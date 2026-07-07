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
    if (!H || side < 2) return 0.0f;
    int grid = side - 1, N = side * side;
    int si, sj; world_to_cell(side, ws, seedX, seedZ, &si, &sj);
    int seed = sj * side + si;

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
        if (i == 0 || j == 0 || i == grid || j == grid) { spill = key; break; }
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

    /* Flood-fill connected vertices below `level`. With max_radius > 0 the fill is
       bounded to a disc around the seed (in world units), so on open/bowl terrain
       it forms a local pool instead of flooding the whole map. */
    int ssi = seed % side, ssj = seed / side;
    float maxCells = (max_radius > 0.0f) ? max_radius / (ws / (float)grid) : 0.0f;
    float maxCells2 = maxCells * maxCells;
    char *in = (char *)calloc((size_t)N, 1);
    int *stack = (int *)malloc((size_t)N * sizeof(int));
    int sp = 0, count = 0;
    float minh = H[seed];
    stack[sp++] = seed; in[seed] = 1;
    while (sp > 0) {
        int c = stack[--sp];
        count++;
        if (H[c] < minh) minh = H[c];
        int i = c % side, j = c / side;
        int nb[4] = { c - 1, c + 1, c - side, c + side };
        int ni[4] = { i - 1, i + 1, i, i };
        int nj[4] = { j, j, j - 1, j + 1 };
        for (int d = 0; d < 4; d++) {
            if (ni[d] < 0 || nj[d] < 0 || ni[d] > grid || nj[d] > grid) continue;
            int n = nb[d];
            if (blocked && blocked[n]) continue;   /* don't flood into a river */
            if (maxCells2 > 0.0f) {
                float di2 = (float)(ni[d] - ssi), dj2 = (float)(nj[d] - ssj);
                if (di2 * di2 + dj2 * dj2 > maxCells2) continue;   /* outside the disc */
            }
            if (!in[n] && H[n] < level) { in[n] = 1; stack[sp++] = n; }
        }
    }
    free(stack);
    if (out_filled)
        for (int k = 0; k < N; k++) if (in[k]) out_filled[k] = 1;

    /* Emit one quad per flooded CELL (a vertex-cell whose 4 corners are flooded
       counts; we use vertex membership of its bottom-left corner for simplicity:
       a cell (i,j) is watered if vertex (i,j) is in the basin). */
    /* Count watered cells. */
    int cells = 0;
    for (int j = 0; j < grid; j++)
        for (int i = 0; i < grid; i++)
            if (in[j * side + i]) cells++;
    if (cells == 0) { free(in); if (out_depth) *out_depth = 0.0f; return NULL; }

    G3DVertex *verts = (G3DVertex *)malloc((size_t)cells * 4 * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)cells * 6 * sizeof(uint32_t));
    int v = 0, ic = 0;
    float cell_w = ws / (float)grid;
    for (int j = 0; j < grid; j++) {
        for (int i = 0; i < grid; i++) {
            if (!in[j * side + i]) continue;
            float x0 = ((float)i / grid - 0.5f) * ws;
            float z0 = ((float)j / grid - 0.5f) * ws;
            float x1 = x0 + cell_w, z1 = z0 + cell_w;
            int base = v;
            float cx[4] = { x0, x1, x0, x1 };
            float cz[4] = { z0, z0, z1, z1 };
            /* terrain height at the 4 corners -> shore depth (surfaceY - terrain),
               packed into texcoord.x so the shader can foam the shallow edges */
            int ci[4] = { i, i + 1, i, i + 1 };
            int cj[4] = { j, j, j + 1, j + 1 };
            for (int k = 0; k < 4; k++) {
                int ic2 = ci[k] > grid ? grid : ci[k];
                int jc2 = cj[k] > grid ? grid : cj[k];
                float th = H[jc2 * side + ic2];
                float sd = surfaceY - th; if (sd < 0.0f) sd = 0.0f;
                verts[v].position[0] = cx[k];
                verts[v].position[1] = surfaceY;   /* water surface height */
                verts[v].position[2] = cz[k];
                verts[v].normal[0] = 0; verts[v].normal[1] = 1; verts[v].normal[2] = 0;
                verts[v].texcoord[0] = sd; verts[v].texcoord[1] = 0;
                v++;
            }
            idx[ic++] = base + 0; idx[ic++] = base + 2; idx[ic++] = base + 1;
            idx[ic++] = base + 1; idx[ic++] = base + 2; idx[ic++] = base + 3;
        }
    }
    free(in);

    G3DMesh *mesh = g3d_mesh_create("lake", verts, (uint32_t)(cells * 4), idx, (uint32_t)(cells * 6));
    free(verts); free(idx);
    if (mesh) g3d_mesh_upload_gpu(mesh);
    if (out_depth) { float d = surfaceY - minh; *out_depth = d > 0.0f ? d : 0.0f; }
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
            /* Glue the surface to the terrain: keep it within a shallow depth of the
               ground AT THIS vertex (sampled from the current, already-carved heights)
               so the water hugs the slope instead of floating as a flat ribbon above
               the hillside. `lev` still caps it (e.g. down to a lake's level). */
            float vy = lev;
            if (th + 0.5f < vy) vy = th + 0.5f;
            float sd = vy - th; if (sd < 0.0f) sd = 0.0f;
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

    G3DMesh *mesh = g3d_mesh_create("river", verts, (uint32_t)vcount, idx, (uint32_t)icount);
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
