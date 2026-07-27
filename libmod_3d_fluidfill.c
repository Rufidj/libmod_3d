/*
 * libmod_3d_fluidfill.c - Flood-fill a terrain depression into a lake surface.
 *
 * Given a shared height array (side*side spanning world_size, centered at the
 * origin, row-major j*side+i), find the level at which water poured at a seed
 * would overflow the basin (its spill rim), and build a flat water mesh covering
 * exactly the flooded cells. Lets the editor/runtime turn a sculpted hole into a
 * lake that conforms to its shape.
 */

#include "libmod_3d_water.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_chunkterrain.h"   /* g3d_heightfield_height */
#include <stdlib.h>
#include <math.h>

/* world (x,z) -> nearest grid vertex (clamped) */
static void world_to_cell(int side, float ws, float wx, float wz, int *oi, int *oj) {
    int grid = side - 1;
    int i = (int)lrintf((wx / ws + 0.5f) * grid);
    int j = (int)lrintf((wz / ws + 0.5f) * grid);
    if (i < 0) i = 0; if (j < 0) j = 0;
    if (i > grid) i = grid; if (j > grid) j = grid;
    *oi = i; *oj = j;
}

/* min-heap of (key,cell) ------------------------------------------------- */
typedef struct { float key; int cell; } HNode;
typedef struct { HNode *a; int n, cap; } Heap;

static void heap_push(Heap *h, float key, int cell) {
    if (h->n >= h->cap) { h->cap = h->cap ? h->cap * 2 : 1024; h->a = (HNode *)realloc(h->a, h->cap * sizeof(HNode)); }
    int i = h->n++;
    h->a[i].key = key; h->a[i].cell = cell;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->a[p].key <= h->a[i].key) break;
        HNode t = h->a[p]; h->a[p] = h->a[i]; h->a[i] = t; i = p;
    }
}
static int heap_pop(Heap *h, float *key, int *cell) {
    if (h->n == 0) return 0;
    *key = h->a[0].key; *cell = h->a[0].cell;
    h->a[0] = h->a[--h->n];
    int i = 0;
    while (1) {
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < h->n && h->a[l].key < h->a[m].key) m = l;
        if (r < h->n && h->a[r].key < h->a[m].key) m = r;
        if (m == i) break;
        HNode t = h->a[m]; h->a[m] = h->a[i]; h->a[i] = t; i = m;
    }
    return 1;
}

float g3d_fluid_spill_level(const float *H, int side, float ws, float seedX, float seedZ,
                            const unsigned char *blocked) {
    return g3d_fluid_spill_level_r(H, side, ws, seedX, seedZ, blocked, 0.0f);
}

/* Como g3d_fluid_spill_level pero acotado a un disco de max_radius: el "borde"
   por el que desborda es el limite del disco (o el del mapa). Asi da el nivel de
   desborde del basin LOCAL, para hoyos abiertos al borde de una montana. */
float g3d_fluid_spill_level_r(const float *H, int side, float ws, float seedX, float seedZ,
                              const unsigned char *blocked, float max_radius) {
    if (!H || side < 2) return 0.0f;
    int grid = side - 1, N = side * side;
    int si, sj; world_to_cell(side, ws, seedX, seedZ, &si, &sj);
    int seed = sj * side + si;
    float maxCells = (max_radius > 0.0f) ? max_radius / (ws / (float)grid) : 0.0f;
    float maxCells2 = maxCells * maxCells;

    /* Dijkstra with a "max height along path" cost: the level needed to reach a
       cell is the largest terrain height on the cheapest path from the seed.
       The spill level is that cost for the first map-border cell we settle. */
    float *best = (float *)malloc((size_t)N * sizeof(float));
    char *done = (char *)calloc((size_t)N, 1);
    for (int k = 0; k < N; k++) best[k] = 1e30f;
    Heap h = {0};
    best[seed] = H[seed];
    heap_push(&h, H[seed], seed);
    float spill = H[seed];

    float key; int c;
    while (heap_pop(&h, &key, &c)) {
        if (done[c]) continue;
        done[c] = 1;
        int i = c % side, j = c / side;
        /* desborda por el borde del mapa O por el limite del disco local */
        if (i == 0 || j == 0 || i == grid || j == grid) { spill = key; break; }
        if (maxCells2 > 0.0f) {
            float di = (float)(i - si), dj = (float)(j - sj);
            if (di * di + dj * dj > maxCells2) { spill = key; break; }
        }
        int nb[4] = { c - 1, c + 1, c - side, c + side };
        int ni[4] = { i - 1, i + 1, i, i };
        int nj[4] = { j, j, j - 1, j + 1 };
        for (int d = 0; d < 4; d++) {
            if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
            int n = nb[d];
            if (blocked && blocked[n]) continue;   /* river/other water = wall */
            float nl = key > H[n] ? key : H[n];   /* max(level so far, neighbour height) */
            if (nl < best[n]) { best[n] = nl; heap_push(&h, nl, n); }
        }
    }
    free(h.a); free(best); free(done);
    return spill;
}

G3DMesh *g3d_fluid_build_lake(const float *H, int side, float ws,
                              float seedX, float seedZ, float footprintLevel,
                              float surfaceY, const unsigned char *blocked,
                              unsigned char *out_filled, float *out_depth,
                              float max_radius) {
    if (!H || side < 2) return NULL;
    int grid = side - 1, N = side * side;
    int si, sj; world_to_cell(side, ws, seedX, seedZ, &si, &sj);
    int seed = sj * side + si;
    float level = footprintLevel;   /* footprint shape = cells below this level */
    /* If the seed cell is no longer below the water level (e.g. the terrain was
       re-sculpted after the lake was created), snap to the nearest cell that is,
       searching outward in rings. Keeps the lake from silently vanishing. */
    if (H[seed] >= level) {
        int found = -1;
        for (int r = 1; r <= grid && found < 0; r++) {
            for (int dj = -r; dj <= r && found < 0; dj++) {
                for (int di = -r; di <= r; di++) {
                    if (di > -r && di < r && dj > -r && dj < r) continue;  /* ring only */
                    int ni = si + di, nj = sj + dj;
                    if (ni < 0 || nj < 0 || ni > grid || nj > grid) continue;
                    if (blocked && blocked[nj * side + ni]) continue;
                    if (H[nj * side + ni] < level) { found = nj * side + ni; break; }
                }
            }
        }
        if (found < 0) { if (out_depth) *out_depth = 0.0f; return NULL; }
        seed = found;
    }

    /* Llenado de CUENCA (watershed), automatico: el lago llena la depresion del
       seed hasta el punto de DESBORDE y NO se escapa por el hueco. Dijkstra
       min-max: best[c] = altura de agua minima para llegar a c desde el seed (el
       maximo del terreno a lo largo del mejor camino). La primera celda de
       borde/disco que se asienta da el nivel de desborde (spill). La CUENCA =
       celdas con best < spill (las ENCERRADAS por el contorno de desborde); el
       cauce de escape cuesta abajo queda a best>=spill, asi que NO se llena. */
    int ssi = seed % side, ssj = seed / side;
    float maxCells = (max_radius > 0.0f) ? max_radius / (ws / (float)grid) : 0.0f;
    float maxCells2 = maxCells * maxCells;
    float *best = (float *)malloc((size_t)N * sizeof(float));
    char *done2 = (char *)calloc((size_t)N, 1);
    for (int k = 0; k < N; k++) best[k] = 1e30f;
    Heap hp = {0};
    best[seed] = H[seed]; heap_push(&hp, H[seed], seed);
    float spillv = 1e30f, keyv; int cc;
    while (heap_pop(&hp, &keyv, &cc)) {
        if (done2[cc]) continue;
        done2[cc] = 1;
        int i = cc % side, j = cc / side;
        int border = (i == 0 || j == 0 || i == grid || j == grid);
        if (!border && maxCells2 > 0.0f) {
            float di = (float)(i - ssi), dj = (float)(j - ssj);
            if (di * di + dj * dj > maxCells2) border = 1;   /* limite del disco = borde */
        }
        if (border) { spillv = keyv; break; }                /* desborde -> paramos */
        int nb[4] = { cc - 1, cc + 1, cc - side, cc + side };
        int ni[4] = { i - 1, i + 1, i, i };
        int nj[4] = { j, j, j - 1, j + 1 };
        for (int d = 0; d < 4; d++) {
            if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
            int n = nb[d];
            if (blocked && blocked[n]) continue;             /* rio = muro */
            float nl = keyv > H[n] ? keyv : H[n];
            if (nl < best[n]) { best[n] = nl; heap_push(&hp, nl, n); }
        }
    }
    free(hp.a); free(done2);
    /* La superficie del agua no puede pasar del desborde (si no, rebosaria). */
    float waterY = surfaceY;
    if (waterY > spillv - 0.05f) waterY = spillv - 0.05f;
    char *in = (char *)calloc((size_t)N, 1);
    int count = 0;
    float minh = H[seed];
    for (int k = 0; k < N; k++) {
        if (best[k] < spillv && H[k] < waterY) {   /* dentro de la cuenca y bajo el agua */
            in[k] = 1; count++;
            if (H[k] < minh) minh = H[k];
        }
    }
    free(best);
    if (out_filled)
        for (int k = 0; k < N; k++) if (in[k]) out_filled[k] = 1;

    /* Superficie del lago = rejilla de cuadrados completos sobre la cuenca,
       DILATADA una celda sobre la orilla. NO se recorta la orilla en la malla
       (eso daba el borde dentado/escalera de la rejilla): la linea de costa se
       dibuja POR PIXEL en el shader (compara la altura del agua con la del terreno
       detras, via depth buffer, y descarta/funde la tierra). Por eso la malla
       cubre un poco de tierra: para que el shader tenga pixeles donde dibujar el
       borde liso. Es la tecnica de los motores 3D (soft shoreline por profundidad). */
    unsigned char *emit = (unsigned char *)calloc((size_t)N, 1);   /* por VERTICE */
    for (int j = 0; j <= grid; j++)
        for (int i = 0; i <= grid; i++) {
            /* marca el vertice si el o algun vecino (3x3) esta en la cuenca -> dilata */
            int hit = 0;
            for (int dj = -1; dj <= 1 && !hit; dj++)
                for (int di = -1; di <= 1; di++) {
                    int ni = i+di, nj = j+dj;
                    if (ni < 0 || nj < 0 || ni > grid || nj > grid) continue;
                    if (in[nj*side+ni]) { hit = 1; break; }
                }
            if (hit) emit[j*side+i] = 1;
        }
    /* cuenta celdas a emitir (las 4 esquinas marcadas) */
    int cells = 0;
    for (int j = 0; j < grid; j++)
        for (int i = 0; i < grid; i++) {
            int c0=j*side+i, c1=j*side+i+1, c2=(j+1)*side+i+1, c3=(j+1)*side+i;
            if (emit[c0] && emit[c1] && emit[c2] && emit[c3]) cells++;
        }
    if (cells == 0) { free(in); free(emit); if (out_depth) *out_depth = 0.0f; return NULL; }

    G3DVertex *verts = (G3DVertex *)malloc((size_t)cells * 4 * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)cells * 6 * sizeof(uint32_t));
    int v = 0, ic = 0;
    for (int j = 0; j < grid; j++) {
        for (int i = 0; i < grid; i++) {
            int cc[4] = { j*side+i, j*side+i+1, (j+1)*side+i+1, (j+1)*side+i };
            if (!emit[cc[0]] || !emit[cc[1]] || !emit[cc[2]] || !emit[cc[3]]) continue;
            float xi = ((float)i / grid - 0.5f) * ws, xi1 = ((float)(i+1) / grid - 0.5f) * ws;
            float zj = ((float)j / grid - 0.5f) * ws, zj1 = ((float)(j+1) / grid - 0.5f) * ws;
            float px[4] = { xi, xi1, xi1, xi };
            float pz[4] = { zj, zj,  zj1, zj1 };
            int base = v;
            for (int k = 0; k < 4; k++) {
                float sd = waterY - H[cc[k]]; if (sd < 0.0f) sd = 0.0f;   /* fallback */
                verts[v].position[0] = px[k];
                verts[v].position[1] = waterY;
                verts[v].position[2] = pz[k];
                verts[v].normal[0] = 0; verts[v].normal[1] = 1; verts[v].normal[2] = 0;
                verts[v].texcoord[0] = sd; verts[v].texcoord[1] = 0;
                v++;
            }
            idx[ic++] = base;   idx[ic++] = base+2; idx[ic++] = base+1;
            idx[ic++] = base;   idx[ic++] = base+3; idx[ic++] = base+2;
        }
    }
    free(in); free(emit);
    if (v < 3 || ic < 3) { free(verts); free(idx); if (out_depth) *out_depth = 0.0f; return NULL; }

    G3DMesh *mesh = g3d_mesh_create("lake", verts, (uint32_t)v, idx, (uint32_t)ic);
    free(verts); free(idx);
    if (mesh) g3d_mesh_upload_gpu(mesh);
    if (out_depth) { float d = waterY - minh; *out_depth = d > 0.0f ? d : 0.0f; }
    return mesh;
}

G3DMesh *g3d_fluid_build_river(const float *pts, int n, const float *H,
                               int side, float ws, float width, float *out_depth) {
    if (!pts || n < 2 || !H || side < 2) return NULL;
    const int COLS = 10;            /* vertices across the width = COLS+1 */
    const float STEP = 2.0f;        /* resample the course every ~2 units (smooth) */
    float hw = width * 0.5f;

    /* Resample the polyline into smooth rows (pos + level + direction). */
    int cap = 16, nrows = 0;
    float *rc = (float *)malloc((size_t)cap * 5 * sizeof(float));  /* cx,cz,level,dx,dz */
    for (int s = 0; s < n - 1; s++) {
        float ax = pts[s * 3], ay = pts[s * 3 + 1], az = pts[s * 3 + 2];
        float bx = pts[(s + 1) * 3], by = pts[(s + 1) * 3 + 1], bz = pts[(s + 1) * 3 + 2];
        float dx = bx - ax, dy = by - ay, dz = bz - az, L = sqrtf(dx * dx + dz * dz);
        if (L < 1e-4f) L = 1e-4f;
        float ndx = dx / L, ndz = dz / L;
        /* subdivide by TRUE 3D length so steep drops get enough rows (not stretched) */
        float L3 = sqrtf(dx * dx + dy * dy + dz * dz);
        int sub = (int)(L3 / STEP); if (sub < 1) sub = 1;
        for (int k = 0; k < sub; k++) {
            float u = (float)k / (float)sub;
            if (nrows >= cap) { cap *= 2; rc = (float *)realloc(rc, (size_t)cap * 5 * sizeof(float)); }
            rc[nrows * 5 + 0] = ax + dx * u;
            rc[nrows * 5 + 1] = az + dz * u;
            rc[nrows * 5 + 2] = ay + (by - ay) * u;
            rc[nrows * 5 + 3] = ndx;
            rc[nrows * 5 + 4] = ndz;
            nrows++;
        }
    }
    /* final row = last point, with the last segment's direction */
    if (nrows >= cap) { cap *= 2; rc = (float *)realloc(rc, (size_t)cap * 5 * sizeof(float)); }
    {
        float ax = pts[(n - 2) * 3], az = pts[(n - 2) * 3 + 2];
        float bx = pts[(n - 1) * 3], bz = pts[(n - 1) * 3 + 2];
        float dx = bx - ax, dz = bz - az, L = sqrtf(dx * dx + dz * dz); if (L < 1e-4f) L = 1e-4f;
        rc[nrows * 5 + 0] = pts[(n - 1) * 3];
        rc[nrows * 5 + 1] = pts[(n - 1) * 3 + 2];
        rc[nrows * 5 + 2] = pts[(n - 1) * 3 + 1];
        rc[nrows * 5 + 3] = dx / L; rc[nrows * 5 + 4] = dz / L;
        nrows++;
    }
    if (nrows < 2) { free(rc); if (out_depth) *out_depth = 0.0f; return NULL; }

    int vcols = COLS + 1;
    int vcount = vcols * nrows;
    int icount = COLS * (nrows - 1) * 6;
    G3DVertex *verts = (G3DVertex *)malloc((size_t)vcount * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!verts || !idx) { free(rc); free(verts); free(idx); if (out_depth) *out_depth = 0.0f; return NULL; }

    float minh = 1e30f, maxlev = -1e30f;
    /* cumulative distance ALONG the course (true 3D length, INCLUDING the vertical
       drop), so the shader maps the surface pattern/texture following the riverbed
       instead of by top-down world XZ. Using 3D length keeps the pattern from
       compressing into bands on steep/vertical sections down a slope. */
    float along = 0.0f, pcx = rc[0], pcz = rc[1], plev = rc[2];
    for (int r = 0; r < nrows; r++) {
        float cx = rc[r * 5 + 0], cz = rc[r * 5 + 1], lev = rc[r * 5 + 2];
        float fdx = rc[r * 5 + 3], fdz = rc[r * 5 + 4];
        float px = -fdz, pz = fdx;   /* perpendicular in XZ */
        float ddx = cx - pcx, ddz = cz - pcz, ddy = lev - plev;
        along += sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
        pcx = cx; pcz = cz; plev = lev;
        if (lev > maxlev) maxlev = lev;
        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)COLS;
            float off = (fu - 0.5f) * width;
            float wx = cx + px * off, wz = cz + pz * off;
            float th = g3d_heightfield_height(H, side, ws, wx, wz);
            if (th < minh) minh = th;
            /* La superficie va al NIVEL del rio (que el llamador ya coloca sobre el
               cauce excavado + la profundidad, asi que sigue la pendiente). La
               PROFUNDIDAD real = nivel - suelo en este vertice; es lo que da el
               color somero->hondo, la espuma de orilla y el borde suave (como en
               los motores pro). En las orillas, donde el banco sube por encima del
               nivel, la profundidad es 0 y se levanta la superficie al suelo para
               que no se meta bajo tierra (borde exacto del agua). */
            float sd = lev - th; if (sd < 0.0f) sd = 0.0f;
            float vy = (lev > th) ? lev : th;
            int vi = r * vcols + i;
            verts[vi].position[0] = wx;
            verts[vi].position[1] = vy;
            verts[vi].position[2] = wz;
            verts[vi].normal[0] = fdx; verts[vi].normal[1] = 0.0f; verts[vi].normal[2] = fdz;
            verts[vi].texcoord[0] = sd; verts[vi].texcoord[1] = along;   /* shore depth, along-course U */
        }
    }
    int t = 0;
    for (int r = 0; r < nrows - 1; r++) {
        /* Si entre esta fila y la siguiente hay una CAIDA fuerte, aqui va una
           cascada (lamina de flujo aparte): NO se emite la superficie del rio en ese
           tramo, para que no se mezcle el mesh plano del rio con la cascada. */
        float drop = rc[r * 5 + 2] - rc[(r + 1) * 5 + 2];
        float ddx = rc[(r + 1) * 5 + 0] - rc[r * 5 + 0];
        float ddz = rc[(r + 1) * 5 + 1] - rc[r * 5 + 1];
        float horiz = sqrtf(ddx * ddx + ddz * ddz) + 1e-4f;
        if (drop > 1.2f && drop > horiz * 0.6f) continue;   /* hueco para la cascada */
        for (int i = 0; i < COLS; i++) {
            uint32_t a = (uint32_t)(r * vcols + i);
            uint32_t b = (uint32_t)(r * vcols + i + 1);
            uint32_t c = (uint32_t)((r + 1) * vcols + i);
            uint32_t d = (uint32_t)((r + 1) * vcols + i + 1);
            idx[t++] = a; idx[t++] = c; idx[t++] = b;
            idx[t++] = b; idx[t++] = c; idx[t++] = d;
        }
    }
    free(rc);
    if (t < 3) { free(verts); free(idx); if (out_depth) *out_depth = 0.0f; return NULL; }

    G3DMesh *mesh = g3d_mesh_create("river", verts, (uint32_t)vcount, idx, (uint32_t)t);
    free(verts); free(idx);
    if (mesh) g3d_mesh_upload_gpu(mesh);
    if (out_depth) { float dd = maxlev - minh; *out_depth = dd > 0.0f ? dd : 0.0f; }
    return mesh;
}

/* Mark the cells within `width` of the river polyline into `mask` (side*side, set
   to 1). Used to keep a lake's flood-fill from spilling into an adjacent river. */
void g3d_river_mark_mask(const float *pts, int n, int side, float ws,
                         float width, unsigned char *mask) {
    if (!pts || n < 2 || !mask || side < 2) return;
    int grid = side - 1;
    /* Mask EXACTLY the river's water width: the lake floods right up to the river
       edge (no dry strip) but can't creep along the channel (no river shape). To
       close the seam fully, trace the river's end INTO the lake so the meshes
       overlap there. */
    float hw = width * 0.5f;
    float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
    for (int i = 0; i < n; i++) {
        float x = pts[i * 3], z = pts[i * 3 + 2];
        if (x < minx) minx = x; if (x > maxx) maxx = x;
        if (z < minz) minz = z; if (z > maxz) maxz = z;
    }
    minx -= hw; maxx += hw; minz -= hw; maxz += hw;
    int i0 = (int)floorf((minx / ws + 0.5f) * grid), i1 = (int)ceilf((maxx / ws + 0.5f) * grid);
    int j0 = (int)floorf((minz / ws + 0.5f) * grid), j1 = (int)ceilf((maxz / ws + 0.5f) * grid);
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
    if (i1 > grid) i1 = grid; if (j1 > grid) j1 = grid;
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float wx = ((float)i / grid - 0.5f) * ws, wz = ((float)j / grid - 0.5f) * ws;
            /* distance to the polyline */
            float best = 1e30f;
            for (int s = 0; s < n - 1; s++) {
                float ax = pts[s * 3], az = pts[s * 3 + 2];
                float bx = pts[(s + 1) * 3], bz = pts[(s + 1) * 3 + 2];
                float vx = bx - ax, vz = bz - az, L = vx * vx + vz * vz;
                float u = (L > 1e-6f) ? ((wx - ax) * vx + (wz - az) * vz) / L : 0.0f;
                if (u < 0) u = 0; if (u > 1) u = 1;
                float ddx = wx - (ax + u * vx), ddz = wz - (az + u * vz);
                float dd = ddx * ddx + ddz * ddz;
                if (dd < best) best = dd;
            }
            if (sqrtf(best) <= hw) mask[j * side + i] = 1;
        }
}

/* Return how many leading points of a river polyline to keep before it first
   enters a lake cell (marked in `mask`). The river is trimmed there so it stops
   at the lake instead of running on under it. Returns n if it never enters. */
int g3d_river_trim_count(const float *pts, int n, int side, float ws,
                         const unsigned char *mask) {
    if (!pts || n < 2 || !mask || side < 2) return n;
    for (int i = 0; i < n; i++) {
        int ci, cj;
        world_to_cell(side, ws, pts[i * 3], pts[i * 3 + 2], &ci, &cj);
        if (mask[cj * side + ci]) return i + 1;   /* keep up to (and incl.) this point */
    }
    return n;
}

/* ============================================================================
   HIDROLOGIA AUTOMATICA: analiza el mapa de alturas y deduce donde va cada agua
   (lagos, rios, cascadas). Algoritmo estandar de terreno/GIS:
     1) Priority-Flood: procesa desde el borde con una cola por altura; da para
        cada celda la altura del AGUA EMBALSADA (filled, con los hoyos rellenos) y
        el receptor (recv) = celda hacia la que drena (cuesta abajo).
     2) Acumulacion de caudal: cada celda aporta 1 (lluvia) y se suma aguas abajo.
     3) Lagos = zonas donde filled > terreno (el agua se embalsa).
     4) Rios = celdas con mucha acumulacion que NO son lago.
   El editor consulta los resultados (lagos y polilineas de rio), excava los
   cauces y construye el agua reutilizando los builders de lagos/rios/cascadas.
   ========================================================================== */
#define HYD_MAXLAKES 512
#define HYD_MAXRIVERS 512
#define HYD_MAXRPTS  600
static struct {
    struct { float x, z, level; } lakes[HYD_MAXLAKES];
    int nlakes;
    struct { float pts[HYD_MAXRPTS * 2]; int n; } rivers[HYD_MAXRIVERS];
    int nrivers;
} g_hyd = {0};

static inline float hyd_cx(int c, int side, float ws) { int grid=side-1; return ((float)(c % side)/grid - 0.5f)*ws; }
static inline float hyd_cz(int c, int side, float ws) { int grid=side-1; return ((float)(c / side)/grid - 0.5f)*ws; }

int g3d_hydrology_analyze(float river_thresh, float min_lake_depth, const unsigned char *exclude) {
    g_hyd.nlakes = 0; g_hyd.nrivers = 0;
    const float *H = NULL; int side = 0; float ws = 0.0f;
    if (!g3d_scene_heightfield(&H, &side, &ws) || side < 2) return 0;
    int grid = side - 1, N = side * side;

    /* SUAVIZA el relieve para el analisis (para que el RUIDO no cree micro-agua):
       Hs = suavizado ligero (2 pasadas) para el flujo/lagos; Hs2 = suavizado AMPLIO
       (referencia del relieve "sin cauces") para saber si una celda esta ENCAJADA
       (un canal/hoyo excavado) frente a la ladera general. El agua se decide sobre el
       relieve suave; los builders usan el relieve real. */
    float *Hs  = (float *)malloc((size_t)N * sizeof(float));  /* ligero (flujo/lagos) */
    float *Hs2 = (float *)malloc((size_t)N * sizeof(float));  /* amplio (ref. encajado) */
    float *Ht  = (float *)malloc((size_t)N * sizeof(float));  /* scratch */
    if (!Hs || !Hs2 || !Ht) { free(Hs); free(Hs2); free(Ht); return 0; }
    #define HYD_BLUR(buf, passes) do { \
        for (int p_ = 0; p_ < (passes); p_++) { \
            for (int j = 0; j <= grid; j++) for (int i = 0; i <= grid; i++) { \
                float sum = 0.0f; int cnt = 0; \
                for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) { \
                    int ni = i+di, nj = j+dj; \
                    if (ni < 0 || nj < 0 || ni > grid || nj > grid) continue; \
                    sum += (buf)[nj*side+ni]; cnt++; } \
                Ht[j*side+i] = sum / cnt; } \
            for (int k = 0; k < N; k++) (buf)[k] = Ht[k]; \
        } } while (0)
    for (int k = 0; k < N; k++) Hs[k] = H[k];
    HYD_BLUR(Hs, 2);                                  /* ligero */
    for (int k = 0; k < N; k++) Hs2[k] = Hs[k];
    HYD_BLUR(Hs2, 6);                                 /* amplio (referencia) */
    #undef HYD_BLUR
    free(Ht);
    H = Hs;   /* el analisis (flujo/lagos) usa el relieve LIGERO */

    float *filled = (float *)malloc((size_t)N * sizeof(float));
    int   *recv   = (int *)  malloc((size_t)N * sizeof(int));
    int   *order  = (int *)  malloc((size_t)N * sizeof(int));
    char  *done   = (char *) calloc((size_t)N, 1);
    if (!filled || !recv || !order || !done) { free(filled);free(recv);free(order);free(done);free(Hs); return 0; }
    for (int k = 0; k < N; k++) { filled[k] = 1e30f; recv[k] = -1; }

    /* 1) Priority-Flood desde el borde del mapa */
    Heap h = {0};
    for (int i = 0; i <= grid; i++) {
        int bc[4] = { i, grid*side + i, i*side, i*side + grid };
        for (int t = 0; t < 4; t++) { int c = bc[t]; if (!done[c]) { done[c]=1; filled[c]=H[c]; heap_push(&h, H[c], c); } }
    }
    int no = 0; float key; int c;
    while (heap_pop(&h, &key, &c)) {
        order[no++] = c;
        int i = c % side, j = c / side;
        int nb[4] = { c-1, c+1, c-side, c+side };
        int ni[4] = { i-1, i+1, i, i };
        int nj[4] = { j, j, j-1, j+1 };
        for (int d = 0; d < 4; d++) {
            if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
            int n = nb[d]; if (done[n]) continue;
            done[n] = 1;
            filled[n] = H[n] > filled[c] ? H[n] : filled[c];   /* embalse: llena hoyos */
            recv[n] = c;                                        /* drena hacia c */
            heap_push(&h, filled[n], n);
        }
    }
    free(h.a); free(done);

    /* 2) Acumulacion de caudal (orden inverso al de asentamiento: de mas alto a mas bajo) */
    float *acc = (float *)malloc((size_t)N * sizeof(float));
    for (int k = 0; k < N; k++) acc[k] = 1.0f;
    for (int o = no - 1; o >= 0; o--) { int cc = order[o]; if (recv[cc] >= 0) acc[recv[cc]] += acc[cc]; }
    free(order);

    /* 3) LAGOS: regiones conexas donde el agua se embalsa (filled > terreno) */
    char *lake = (char *)calloc((size_t)N, 1);
    for (int k = 0; k < N; k++)
        if (filled[k] > H[k] + min_lake_depth && !(exclude && exclude[k])) lake[k] = 1;
    char *vis = (char *)calloc((size_t)N, 1);
    int  *stk = (int *)malloc((size_t)N * sizeof(int));
    for (int s0 = 0; s0 < N && g_hyd.nlakes < HYD_MAXLAKES; s0++) {
        if (!lake[s0] || vis[s0]) continue;
        int sp = 0; stk[sp++] = s0; vis[s0] = 1;
        int seed = s0; float minH = H[s0]; float lvl = filled[s0]; int cells = 0;
        while (sp > 0) {
            int c2 = stk[--sp]; cells++;
            if (H[c2] < minH) { minH = H[c2]; seed = c2; }
            int i = c2 % side, j = c2 / side;
            int nb[4] = { c2-1, c2+1, c2-side, c2+side };
            int ni[4] = { i-1, i+1, i, i }, nj[4] = { j, j, j-1, j+1 };
            for (int d = 0; d < 4; d++) {
                if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
                int n = nb[d]; if (lake[n] && !vis[n]) { vis[n] = 1; stk[sp++] = n; }
            }
        }
        int minLakeCells = N / 1200; if (minLakeCells < 15) minLakeCells = 15;
        if (cells < minLakeCells) continue;   /* charco insignificante -> no es lago */
        g_hyd.lakes[g_hyd.nlakes].x = hyd_cx(seed, side, ws);
        g_hyd.lakes[g_hyd.nlakes].z = hyd_cz(seed, side, ws);
        g_hyd.lakes[g_hyd.nlakes].level = lvl;
        g_hyd.nlakes++;
    }

    /* 4) RIOS: celdas con mucho caudal que NO son lago; se trazan desde las
          cabeceras (sin rio aguas arriba) siguiendo recv hasta un lago/mar/borde */
    char *river = (char *)calloc((size_t)N, 1);
    for (int k = 0; k < N; k++)
        if (acc[k] > river_thresh && !lake[k] && !(exclude && exclude[k])
            && H[k] < Hs2[k] - 0.6f)   /* ENCAJADO: mas bajo que la ladera general
                                          -> solo rellena cauces excavados, no laderas */
            river[k] = 1;
    char *used = (char *)calloc((size_t)N, 1);
    for (int s0 = 0; s0 < N && g_hyd.nrivers < HYD_MAXRIVERS; s0++) {
        if (!river[s0] || used[s0]) continue;
        int i0 = s0 % side, j0 = s0 / side, isHead = 1;
        int nb[4] = { s0-1, s0+1, s0-side, s0+side };
        int ni[4] = { i0-1, i0+1, i0, i0 }, nj[4] = { j0, j0, j0-1, j0+1 };
        for (int d = 0; d < 4; d++) {
            if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
            int n = nb[d]; if (river[n] && recv[n] == s0) { isHead = 0; break; }
        }
        if (!isHead) continue;
        int rp = 0, cur = s0;
        while (cur >= 0 && river[cur] && !used[cur] && rp < HYD_MAXRPTS) {
            used[cur] = 1;
            g_hyd.rivers[g_hyd.nrivers].pts[rp*2]   = hyd_cx(cur, side, ws);
            g_hyd.rivers[g_hyd.nrivers].pts[rp*2+1] = hyd_cz(cur, side, ws);
            rp++;
            cur = recv[cur];
        }
        /* incluye la celda final (entrada al lago/mar) para que conecte */
        if (cur >= 0 && rp < HYD_MAXRPTS) {
            g_hyd.rivers[g_hyd.nrivers].pts[rp*2]   = hyd_cx(cur, side, ws);
            g_hyd.rivers[g_hyd.nrivers].pts[rp*2+1] = hyd_cz(cur, side, ws);
            rp++;
        }
        if (rp >= 2) { g_hyd.rivers[g_hyd.nrivers].n = rp; g_hyd.nrivers++; }
    }

    free(filled); free(recv); free(acc); free(lake); free(vis); free(stk); free(river); free(used);
    free(Hs); free(Hs2);   /* relieve suavizado (H apuntaba a Hs) */
    return 1;
}

int  g3d_hydrology_lake_count(void) { return g_hyd.nlakes; }
void g3d_hydrology_lake(int i, float *x, float *z, float *level) {
    if (i < 0 || i >= g_hyd.nlakes) return;
    if (x) *x = g_hyd.lakes[i].x; if (z) *z = g_hyd.lakes[i].z; if (level) *level = g_hyd.lakes[i].level;
}
int  g3d_hydrology_river_count(void) { return g_hyd.nrivers; }
int  g3d_hydrology_river_len(int i) { return (i>=0 && i<g_hyd.nrivers) ? g_hyd.rivers[i].n : 0; }
void g3d_hydrology_river_point(int i, int k, float *x, float *z) {
    if (i < 0 || i >= g_hyd.nrivers || k < 0 || k >= g_hyd.rivers[i].n) return;
    if (x) *x = g_hyd.rivers[i].pts[k*2]; if (z) *z = g_hyd.rivers[i].pts[k*2+1];
}
