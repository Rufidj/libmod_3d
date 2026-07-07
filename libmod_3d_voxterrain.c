/*
 * libmod_3d_voxterrain.c - Full voxel terrain via chunked marching cubes.
 *
 * ONE 3D density field (dens>0 solid, <0 air) over the map, meshed in XZ chunks
 * (each chunk samples the shared field incl. a 1-voxel border, so chunk surfaces
 * meet seamlessly). Initialized from a heightmap. A dig/add sphere brush sculpts
 * it; only touched chunks re-mesh. A derived top-surface height feeds water sim
 * and collision.
 */

#include "libmod_3d_voxterrain.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_chunkterrain.h"   /* g3d_heightfield_height */
#include "libmod_3d_water.h"          /* g3d_water_draw_mesh */
#include "libmod_3d_mctables.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define VT_CW 16                 /* terrain cells per chunk side */
#define VT_DMAX 1000.0f          /* density clamp: big, so a deep column stays solid
                                    (a low clamp made "lower" punch holes to the sky) */

static struct {
    int active;
    int nx, ny, nz;              /* voxel grid dimensions */
    float ws;                    /* world size (X and Z span, centred on origin) */
    float ymin, ymax;
    float sx, sy, sz;            /* world units per voxel */
    float *dens;                 /* nx*ny*nz */
    int nchx, nchz;              /* chunk counts */
    G3DMesh **cmesh;             /* nchx*nchz */
    int *cent;                   /* entities */
    unsigned char *cdirty;
    int scene, material;
} V = {0};

static inline int vidx(int x, int y, int z) { return x + V.nx * (y + V.ny * z); }

static inline float dens_at(int x, int y, int z) {
    if (x < 0) x = 0; else if (x >= V.nx) x = V.nx - 1;
    if (y < 0) y = 0; else if (y >= V.ny) y = V.ny - 1;
    if (z < 0) z = 0; else if (z >= V.nz) z = V.nz - 1;
    return V.dens[vidx(x, y, z)];
}

static inline float wx_of(int x) { return -V.ws * 0.5f + x * V.sx; }
static inline float wy_of(int y) { return V.ymin + y * V.sy; }
static inline float wz_of(int z) { return -V.ws * 0.5f + z * V.sz; }

static void vt_free(void) {
    g3d_voxwater_clear();   /* the water lives on this grid */
    free(V.dens); V.dens = NULL;
    if (V.cmesh) {
        int n = V.nchx * V.nchz;
        for (int i = 0; i < n; i++) if (V.cmesh[i]) g3d_mesh_free(V.cmesh[i]);
        free(V.cmesh); V.cmesh = NULL;
    }
    if (V.cent) {
        int n = V.nchx * V.nchz;
        for (int i = 0; i < n; i++) if (V.cent[i] >= 0) g3d_entity_impl_destroy(V.cent[i]);
        free(V.cent); V.cent = NULL;
    }
    free(V.cdirty); V.cdirty = NULL;
}

void g3d_voxterrain_shutdown(void) { vt_free(); V.active = 0; }
int  g3d_voxterrain_active(void) { return V.active; }

void g3d_voxterrain_init_from_heightfield(const float *H, int side, float ws,
                                          int ny, float ymin, float ymax) {
    if (!H || side < 2 || ny < 4) return;
    vt_free();
    V.nx = V.nz = side; V.ny = ny; V.ws = ws; V.ymin = ymin; V.ymax = ymax;
    V.sx = ws / (float)(side - 1); V.sz = V.sx;
    V.sy = (ymax - ymin) / (float)(ny - 1);
    size_t n = (size_t)V.nx * V.ny * V.nz;
    V.dens = (float *)malloc(n * sizeof(float));
    for (int z = 0; z < V.nz; z++) for (int x = 0; x < V.nx; x++) {
        float th = H[z * side + x];           /* heightmap grid == voxel XZ grid */
        for (int y = 0; y < V.ny; y++) {
            float d = th - wy_of(y);            /* >0 below ground (solid), true signed height */
            if (d > VT_DMAX) d = VT_DMAX; else if (d < -VT_DMAX) d = -VT_DMAX;
            V.dens[vidx(x, y, z)] = d;
        }
    }
    V.nchx = (V.nx - 1 + VT_CW - 1) / VT_CW;
    V.nchz = (V.nz - 1 + VT_CW - 1) / VT_CW;
    int nc = V.nchx * V.nchz;
    V.cmesh = (G3DMesh **)calloc((size_t)nc, sizeof(G3DMesh *));
    V.cent = (int *)malloc((size_t)nc * sizeof(int));
    for (int i = 0; i < nc; i++) V.cent[i] = -1;
    V.cdirty = (unsigned char *)calloc((size_t)nc, 1);
    V.active = 1;
}

static void vt_gradient(int x, int y, int z, float *gx, float *gy, float *gz) {
    *gx = dens_at(x + 1, y, z) - dens_at(x - 1, y, z);
    *gy = dens_at(x, y + 1, z) - dens_at(x, y - 1, z);
    *gz = dens_at(x, y, z + 1) - dens_at(x, y, z - 1);
}

/* Mesh chunk (cxi,czi): cells x in [x0,x1), z in [z0,z1), all y. Samples the
   shared field (incl. borders via dens_at clamp) -> seamless with neighbours. */
static void vt_mesh_chunk(int cxi, int czi) {
    int ci = czi * V.nchx + cxi;
    int x0 = cxi * VT_CW, x1 = x0 + VT_CW; if (x1 > V.nx - 1) x1 = V.nx - 1;
    int z0 = czi * VT_CW, z1 = z0 + VT_CW; if (z1 > V.nz - 1) z1 = V.nz - 1;

    int cap = 4096, nv = 0;
    G3DVertex *verts = (G3DVertex *)malloc((size_t)cap * sizeof(G3DVertex));

    for (int z = z0; z < z1; z++)
    for (int y = 0; y < V.ny - 1; y++)
    for (int x = x0; x < x1; x++) {
        float val[8]; float px[8], py[8], pz[8]; float nx[8], ny_[8], nz[8];
        int cubeindex = 0;
        for (int ci8 = 0; ci8 < 8; ci8++) {
            int ii = x + cornerOff[ci8][0], jj = y + cornerOff[ci8][1], kk = z + cornerOff[ci8][2];
            val[ci8] = dens_at(ii, jj, kk);
            px[ci8] = wx_of(ii); py[ci8] = wy_of(jj); pz[ci8] = wz_of(kk);
            vt_gradient(ii, jj, kk, &nx[ci8], &ny_[ci8], &nz[ci8]);
            if (val[ci8] > 0.0f) cubeindex |= (1 << ci8);   /* solid inside -> faces to air */
        }
        int edges = edgeTable[cubeindex];
        if (edges == 0) continue;
        float ex[12], ey[12], ez[12], enx[12], eny[12], enz[12];
        for (int e = 0; e < 12; e++) {
            if (!(edges & (1 << e))) continue;
            int a = edgeCorner[e][0], b = edgeCorner[e][1];
            float t = (val[b] == val[a]) ? 0.5f : (0.0f - val[a]) / (val[b] - val[a]);
            ex[e] = px[a] + t * (px[b] - px[a]);
            ey[e] = py[a] + t * (py[b] - py[a]);
            ez[e] = pz[a] + t * (pz[b] - pz[a]);
            float gx = nx[a] + t * (nx[b] - nx[a]);
            float gy = ny_[a] + t * (ny_[b] - ny_[a]);
            float gz = nz[a] + t * (nz[b] - nz[a]);
            float gl = sqrtf(gx * gx + gy * gy + gz * gz); if (gl < 1e-6f) gl = 1.0f;
            enx[e] = -gx / gl; eny[e] = -gy / gl; enz[e] = -gz / gl;
        }
        for (int t = 0; triTable[cubeindex][t] != -1; t += 3) {
            if (nv + 3 > cap) { cap *= 2; verts = (G3DVertex *)realloc(verts, (size_t)cap * sizeof(G3DVertex)); }
            for (int w = 0; w < 3; w++) {
                int e = triTable[cubeindex][t + w];
                G3DVertex *Vt = &verts[nv++];
                Vt->position[0] = ex[e]; Vt->position[1] = ey[e]; Vt->position[2] = ez[e];
                Vt->normal[0] = enx[e]; Vt->normal[1] = eny[e]; Vt->normal[2] = enz[e];
                /* same UV as the heightmap chunks (top-down) so the painted terrain
                   texture aligns on the surface (walls stretch until triplanar) */
                Vt->texcoord[0] = ex[e] / V.ws + 0.5f; Vt->texcoord[1] = ez[e] / V.ws + 0.5f;
            }
        }
    }

    /* replace the chunk mesh */
    if (V.cmesh[ci]) { g3d_mesh_free(V.cmesh[ci]); V.cmesh[ci] = NULL; }
    G3DMesh *m = NULL;
    if (nv > 0) {
        uint32_t *idx = (uint32_t *)malloc((size_t)nv * sizeof(uint32_t));
        for (int i = 0; i < nv; i++) idx[i] = (uint32_t)i;
        m = g3d_mesh_create("voxchunk", verts, (uint32_t)nv, idx, (uint32_t)nv);
        free(idx);
        if (m) g3d_mesh_upload_gpu(m);
    }
    V.cmesh[ci] = m;
    free(verts);

    /* attach to an entity (spawn once) */
    if (V.cent[ci] < 0) {
        V.cent[ci] = g3d_entity_impl_spawn(V.scene, 0, 0.0f, 0.0f, 0.0f);
    }
    G3DEntity *ee = g3d_entity_impl_get(V.cent[ci]);
    if (ee) { ee->mesh = m; ee->material_id = V.material; ee->active = (m != NULL); }
    V.cdirty[ci] = 0;
}

void g3d_voxterrain_build(int scene, int material) {
    if (!V.active) return;
    V.scene = scene; V.material = material;
    for (int czi = 0; czi < V.nchz; czi++)
        for (int cxi = 0; cxi < V.nchx; cxi++)
            vt_mesh_chunk(cxi, czi);
}

void g3d_voxterrain_edit(float wx, float wy, float wz, float radius, int mode, float strength) {
    if (!V.active) return;
    if (strength <= 0.0f) strength = 1.0f;
    int i0 = (int)floorf((wx - radius + V.ws * 0.5f) / V.sx), i1 = (int)ceilf((wx + radius + V.ws * 0.5f) / V.sx);
    int k0 = (int)floorf((wz - radius + V.ws * 0.5f) / V.sz), k1 = (int)ceilf((wz + radius + V.ws * 0.5f) / V.sz);
    int j0 = (int)floorf((wy - radius - V.ymin) / V.sy), j1 = (int)ceilf((wy + radius - V.ymin) / V.sy);
    if (i0 < 0) i0 = 0; if (k0 < 0) k0 = 0; if (j0 < 0) j0 = 0;
    if (i1 > V.nx - 1) i1 = V.nx - 1; if (k1 > V.nz - 1) k1 = V.nz - 1; if (j1 > V.ny - 1) j1 = V.ny - 1;
    for (int z = k0; z <= k1; z++) for (int y = j0; y <= j1; y++) for (int x = i0; x <= i1; x++) {
        float dx = wx_of(x) - wx, dy = wy_of(y) - wy, dz = wz_of(z) - wz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > radius) continue;
        float delta = strength * ((radius - d) / radius) * 2.0f;
        float *c = &V.dens[vidx(x, y, z)];
        *c += (mode == 0) ? -delta : delta;
        if (*c > VT_DMAX) *c = VT_DMAX; else if (*c < -VT_DMAX) *c = -VT_DMAX;
    }
    /* mark dirty chunks (a bit of margin so seam borders rebuild too) */
    int cx0 = (i0 - 1) / VT_CW, cx1 = (i1) / VT_CW;
    int cz0 = (k0 - 1) / VT_CW, cz1 = (k1) / VT_CW;
    if (cx0 < 0) cx0 = 0; if (cz0 < 0) cz0 = 0;
    if (cx1 > V.nchx - 1) cx1 = V.nchx - 1; if (cz1 > V.nchz - 1) cz1 = V.nchz - 1;
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++)
        V.cdirty[cz * V.nchx + cx] = 1;
}

void g3d_voxterrain_raise(float wx, float wz, float radius, float delta) {
    if (!V.active) return;
    int i0 = (int)floorf((wx - radius + V.ws * 0.5f) / V.sx), i1 = (int)ceilf((wx + radius + V.ws * 0.5f) / V.sx);
    int k0 = (int)floorf((wz - radius + V.ws * 0.5f) / V.sz), k1 = (int)ceilf((wz + radius + V.ws * 0.5f) / V.sz);
    if (i0 < 0) i0 = 0; if (k0 < 0) k0 = 0;
    if (i1 > V.nx - 1) i1 = V.nx - 1; if (k1 > V.nz - 1) k1 = V.nz - 1;
    for (int z = k0; z <= k1; z++) for (int x = i0; x <= i1; x++) {
        float dx = wx_of(x) - wx, dz = wz_of(z) - wz;
        float d = sqrtf(dx * dx + dz * dz);
        if (d >= radius) continue;
        float t = 1.0f - d / radius; float f = t * t * (3.0f - 2.0f * t);   /* smoothstep */
        float add = delta * f;                       /* shifts the surface crossing by ~add */
        for (int y = 0; y < V.ny; y++) {
            float *c = &V.dens[vidx(x, y, z)];
            *c += add;
            if (*c > VT_DMAX) *c = VT_DMAX; else if (*c < -VT_DMAX) *c = -VT_DMAX;
        }
    }
    int cx0 = (i0 - 1) / VT_CW, cx1 = i1 / VT_CW, cz0 = (k0 - 1) / VT_CW, cz1 = k1 / VT_CW;
    if (cx0 < 0) cx0 = 0; if (cz0 < 0) cz0 = 0;
    if (cx1 > V.nchx - 1) cx1 = V.nchx - 1; if (cz1 > V.nchz - 1) cz1 = V.nchz - 1;
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++)
        V.cdirty[cz * V.nchx + cx] = 1;
}

void g3d_voxterrain_remesh_dirty(void) {
    if (!V.active) return;
    for (int czi = 0; czi < V.nchz; czi++)
        for (int cxi = 0; cxi < V.nchx; cxi++)
            if (V.cdirty[czi * V.nchx + cxi]) vt_mesh_chunk(cxi, czi);
}

float g3d_voxterrain_surface(float x, float z) {
    if (!V.active) return -1e30f;
    int xi = (int)lrintf((x + V.ws * 0.5f) / V.sx);
    int zi = (int)lrintf((z + V.ws * 0.5f) / V.sz);
    if (xi < 0) xi = 0; if (zi < 0) zi = 0;
    if (xi > V.nx - 1) xi = V.nx - 1; if (zi > V.nz - 1) zi = V.nz - 1;
    for (int y = V.ny - 1; y > 0; y--) {
        float du = V.dens[vidx(xi, y, zi)];       /* upper */
        float dl = V.dens[vidx(xi, y - 1, zi)];   /* lower */
        if (du < 0.0f && dl >= 0.0f) {            /* air over solid: surface crossing */
            float t = (0.0f - dl) / (du - dl);
            return wy_of(y - 1) + t * V.sy;
        }
    }
    return -1e30f;
}

void g3d_voxterrain_export_heights(float *out) {
    if (!V.active || !out) return;
    for (int z = 0; z < V.nz; z++) for (int x = 0; x < V.nx; x++) {
        float s = V.ymin;
        for (int y = V.ny - 1; y > 0; y--) {
            float du = V.dens[vidx(x, y, z)], dl = V.dens[vidx(x, y - 1, z)];
            if (du < 0.0f && dl >= 0.0f) { float t = (0.0f - dl) / (du - dl); s = wy_of(y - 1) + t * V.sy; break; }
        }
        out[z * V.nx + x] = s;
    }
}

int g3d_voxterrain_side(void) { return V.active ? V.nx : 0; }

static float dens_sample(float wx, float wy, float wz) {
    float fx = (wx + V.ws * 0.5f) / V.sx, fy = (wy - V.ymin) / V.sy, fz = (wz + V.ws * 0.5f) / V.sz;
    int i = (int)floorf(fx), j = (int)floorf(fy), k = (int)floorf(fz);
    float tx = fx - i, ty = fy - j, tz = fz - k;
    float c00 = dens_at(i, j, k)     * (1 - tx) + dens_at(i + 1, j, k)     * tx;
    float c10 = dens_at(i, j + 1, k) * (1 - tx) + dens_at(i + 1, j + 1, k) * tx;
    float c01 = dens_at(i, j, k + 1) * (1 - tx) + dens_at(i + 1, j, k + 1) * tx;
    float c11 = dens_at(i, j + 1, k + 1) * (1 - tx) + dens_at(i + 1, j + 1, k + 1) * tx;
    float c0 = c00 * (1 - ty) + c10 * ty, c1 = c01 * (1 - ty) + c11 * ty;
    return c0 * (1 - tz) + c1 * tz;
}

int g3d_voxterrain_raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                           float maxdist, float *hx, float *hy, float *hz) {
    if (!V.active) return 0;
    float dl = sqrtf(dx * dx + dy * dy + dz * dz); if (dl < 1e-6f) return 0;
    dx /= dl; dy /= dl; dz /= dl;
    float step = V.sx * 0.4f; if (step < 0.05f) step = 0.05f;
    float prev = dens_sample(ox, oy, oz);
    for (float t = step; t <= maxdist; t += step) {
        float x = ox + dx * t, y = oy + dy * t, z = oz + dz * t;
        float d = dens_sample(x, y, z);
        if (prev < 0.0f && d >= 0.0f) { if (hx) *hx = x; if (hy) *hy = y; if (hz) *hz = z; return 1; }
        prev = d;
    }
    return 0;
}

int g3d_voxterrain_solid(float x, float y, float z) {
    return (V.active && dens_sample(x, y, z) > 0.0f) ? 1 : 0;
}

/* Would a character capsule (feet at y, `radius` wide, `height` tall) overlap rock
   at (x,z)? Samples the capsule perimeter at several heights -> robust wall/ceiling
   collision (a single point leaks through). */
int g3d_voxterrain_blocked(float x, float y, float z, float radius, float height) {
    if (!V.active) return 0;
    float hs[4] = { 0.4f, height * 0.4f, height * 0.75f, height };
    float ox[5] = { 0.0f, radius, -radius, 0.0f, 0.0f };
    float oz[5] = { 0.0f, 0.0f, 0.0f, radius, -radius };
    for (int h = 0; h < 4; h++)
        for (int k = 0; k < 5; k++)
            if (dens_sample(x + ox[k], y + hs[h], z + oz[k]) > 0.0f) return 1;
    return 0;
}

float g3d_voxterrain_worldsize(void) { return V.active ? V.ws : 0.0f; }

/* Slope-aware character move: slide along X then Z, stepping UP onto the ground
   ahead if it rises no more than `maxstep` (walkable hill) but blocking against
   steeper walls/cliffs and low ceilings. Result read back with _walkx/y/z. */
static float g_vt_wx, g_vt_wy, g_vt_wz;

/* Can a character stand at (x,z), feet ~y? Walkable if the ground here rises no
   more than maxstep (a hill, not a wall/cliff), the LEADING edge (radius ahead in
   the travel direction) isn't a wall to walk into, and there is body headroom
   above (no low ceiling). Only the front is checked -- not the sides -- so you can
   hug/slide along walls and pass narrow gaps. dirx/dirz = travel direction. */
static int vt_walkable(float x, float z, float y, float radius, float height,
                       float maxstep, float dirx, float dirz, float *outFloor) {
    float f = g3d_voxterrain_floor(x, y + maxstep, z);
    if (f <= -1e29f || (f - y) > maxstep) return 0;
    float lf = g3d_voxterrain_floor(x + dirx * radius, y + maxstep, z + dirz * radius);
    if (lf > -1e29f && (lf - y) > maxstep) return 0;      /* wall right ahead */
    for (float h = maxstep + 0.5f; h <= height; h += 1.0f)   /* ceiling above the floor? */
        if (dens_sample(x, f + h, z) > 0.0f) return 0;
    *outFloor = f;
    return 1;
}

void g3d_voxterrain_walk(float px, float py, float pz, float radius, float height,
                         float maxstep, float dx, float dz) {
    if (!V.active) { g_vt_wx = px + dx; g_vt_wy = py; g_vt_wz = pz + dz; return; }
    float x = px, z = pz, y = py, nf;
    if (dx != 0.0f && vt_walkable(px + dx, pz, y, radius, height, maxstep, (dx > 0 ? 1 : -1), 0, &nf)) { x = px + dx; y = nf; }
    if (dz != 0.0f && vt_walkable(x, pz + dz, y, radius, height, maxstep, 0, (dz > 0 ? 1 : -1), &nf)) { z = pz + dz; y = nf; }
    g_vt_wx = x; g_vt_wy = y; g_vt_wz = z;
}
float g3d_voxterrain_walkx(void) { return g_vt_wx; }
float g3d_voxterrain_walky(void) { return g_vt_wy; }
float g3d_voxterrain_walkz(void) { return g_vt_wz; }

float g3d_voxterrain_floor(float x, float y, float z) {
    if (!V.active) return -1e30f;
    float step = V.sy * 0.5f; if (step < 0.05f) step = 0.05f;
    float d0 = dens_sample(x, y, z);
    if (d0 > 0.0f) {
        /* inside rock: find the surface ABOVE (so we push the character up onto it) */
        float prev = d0, py = y;
        for (float yy = y + step; yy <= V.ymax; yy += step) {
            float d = dens_sample(x, yy, z);
            if (prev >= 0.0f && d < 0.0f) return py + (yy - py) * (prev / (prev - d));
            prev = d; py = yy;
        }
        return y;
    }
    /* in air: find the solid surface BELOW (the floor to stand on) */
    float prev = d0, py = y;
    for (float yy = y - step; yy >= V.ymin; yy -= step) {
        float d = dens_sample(x, yy, z);
        if (prev < 0.0f && d >= 0.0f) return py + (yy - py) * (-prev / (d - prev));
        prev = d; py = yy;
    }
    return -1e30f;
}

#define VT_MAGIC 0x56543031   /* "VT01" */

int g3d_voxterrain_save(const char *path) {
    if (!V.active || !path) return 0;
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    int magic = VT_MAGIC;
    fwrite(&magic, sizeof(int), 1, f);
    fwrite(&V.nx, sizeof(int), 1, f); fwrite(&V.ny, sizeof(int), 1, f); fwrite(&V.nz, sizeof(int), 1, f);
    fwrite(&V.ws, sizeof(float), 1, f); fwrite(&V.ymin, sizeof(float), 1, f); fwrite(&V.ymax, sizeof(float), 1, f);
    fwrite(V.dens, sizeof(float), (size_t)V.nx * V.ny * V.nz, f);
    fclose(f);
    return 1;
}

int g3d_voxterrain_load(const char *path, int scene, int material) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    int magic = 0; if (fread(&magic, sizeof(int), 1, f) != 1 || magic != VT_MAGIC) { fclose(f); return 0; }
    int nx, ny, nz; float ws, ymin, ymax;
    if (fread(&nx, sizeof(int), 1, f) != 1 || fread(&ny, sizeof(int), 1, f) != 1 || fread(&nz, sizeof(int), 1, f) != 1 ||
        fread(&ws, sizeof(float), 1, f) != 1 || fread(&ymin, sizeof(float), 1, f) != 1 || fread(&ymax, sizeof(float), 1, f) != 1) {
        fclose(f); return 0;
    }
    vt_free();
    V.nx = nx; V.ny = ny; V.nz = nz; V.ws = ws; V.ymin = ymin; V.ymax = ymax;
    V.sx = ws / (float)(nx - 1); V.sz = ws / (float)(nz - 1); V.sy = (ymax - ymin) / (float)(ny - 1);
    size_t n = (size_t)nx * ny * nz;
    V.dens = (float *)malloc(n * sizeof(float));
    if (fread(V.dens, sizeof(float), n, f) != n) { fclose(f); vt_free(); return 0; }
    fclose(f);
    V.nchx = (nx - 1 + VT_CW - 1) / VT_CW; V.nchz = (nz - 1 + VT_CW - 1) / VT_CW;
    int nc = V.nchx * V.nchz;
    V.cmesh = (G3DMesh **)calloc((size_t)nc, sizeof(G3DMesh *));
    V.cent = (int *)malloc((size_t)nc * sizeof(int));
    for (int i = 0; i < nc; i++) V.cent[i] = -1;
    V.cdirty = (unsigned char *)calloc((size_t)nc, 1);
    V.active = 1;
    g3d_voxterrain_build(scene, material);
    return 1;
}

/* ===================================================================
   3D VOXEL WATER  ---  unified real-time fluid on the voxel grid.

   Finite-volume cellular water WITH PRESSURE (a cell under a full cell can hold a
   little extra, so connected water communicates and finds a common level): water
   pools flat, flows through gaps, cascades down and fills caves in true 3D. Only
   the active (wet) region is simulated, so it stays fast. Springs are real sources.
   =================================================================== */

#define VW_MAXMASS     1.0f
#define VW_MAXCOMPRESS 0.02f     /* extra a lower cell holds -> pressure/communication */
#define VW_MINMASS     0.0001f   /* below this a cell is considered dry */
#define VW_MAXSPEED    1.0f      /* max mass moved across a face per substep */
#define VW_VIS         0.10f     /* min mass to draw (kills thin films / floating specks) */
#define VW_EVAP        0.02f     /* mass removed per cell per substep: the engine trims the
                                    excess so a steady spring reaches a stable level (thin
                                    overflow evaporates before it floods; the fed lake stays) */
#define VW_MARGIN      2         /* fill the basin to this many voxels BELOW its brim, so the
                                    water sits a bit inside the rim and never leaks the notches */

static float *g_vw  = NULL;      /* water mass per voxel */
static float *g_vw2 = NULL;      /* scratch (double buffer) */
static unsigned char *g_vw_basin = NULL;  /* 1 = a cell water is allowed in (inside a filled
                                             basin, up to margin below its brim); water
                                             elsewhere is removed -> nothing escapes */
static struct { float x, y, z, r; } g_vwsrc[256];
static int    g_nvwsrc = 0;
static G3DMesh *g_vw_mesh = NULL;
static int    g_vw_dirty = 0;
static int    g_vw_i0 = 1, g_vw_i1 = 0, g_vw_j0 = 0, g_vw_j1 = 0, g_vw_k0 = 0, g_vw_k1 = 0;

static inline int vw_air(int i, int j, int k) { return dens_at(i, j, k) < 0.0f; }
static void vw_box_add(int i, int j, int k) {
    if (g_vw_i0 > g_vw_i1) { g_vw_i0 = g_vw_i1 = i; g_vw_j0 = g_vw_j1 = j; g_vw_k0 = g_vw_k1 = k; return; }
    if (i < g_vw_i0) g_vw_i0 = i; if (i > g_vw_i1) g_vw_i1 = i;
    if (j < g_vw_j0) g_vw_j0 = j; if (j > g_vw_j1) g_vw_j1 = j;
    if (k < g_vw_k0) g_vw_k0 = k; if (k > g_vw_k1) g_vw_k1 = k;
}

int g3d_voxwater_active(void) { return (V.active && g_vw) ? 1 : 0; }

void g3d_voxwater_clear(void) {
    free(g_vw);  g_vw  = NULL;
    free(g_vw2); g_vw2 = NULL;
    free(g_vw_basin); g_vw_basin = NULL;
    g_nvwsrc = 0;
    if (g_vw_mesh) { g3d_mesh_free(g_vw_mesh); g_vw_mesh = NULL; }
    g_vw_i0 = 1; g_vw_i1 = 0; g_vw_dirty = 0;
}

/* Basin SPILL level (voxel j) reachable from a seed: priority-flood by height until the
   water would escape (map edge / world top). The basin fills to just below this. */
static int vw_spill_level(int si, int sj, int sk) {
    if (!vw_air(si, sj, sk)) return V.ny;
    int NX = V.nx, NY = V.ny, NZ = V.nz; size_t N = (size_t)NX * NY * NZ;
    unsigned char *vis = (unsigned char *)calloc(N, 1);
    int  *bcnt = (int *) calloc((size_t)NY, sizeof(int));
    int  *bcap = (int *) calloc((size_t)NY, sizeof(int));
    int **bkt  = (int **)calloc((size_t)NY, sizeof(int *));
    if (!vis || !bcnt || !bcap || !bkt) { free(vis); free(bcnt); free(bcap); free(bkt); return 1e30f; }
    #define VW_SP(x,y,z,b) do { int _c = vidx(x,y,z); if (!vis[_c]) { vis[_c]=1; int _b=(b); \
        if (bcnt[_b]>=bcap[_b]) { bcap[_b]=bcap[_b]?bcap[_b]*2:64; bkt[_b]=(int*)realloc(bkt[_b],(size_t)bcap[_b]*sizeof(int)); } \
        bkt[_b][bcnt[_b]++]=_c; } } while (0)
    VW_SP(si, sj, sk, sj);
    int spill = -1, maxj = sj;
    for (int lv = 0; lv < NY && spill < 0; lv++) {
        while (bcnt[lv] > 0) {
            int c = bkt[lv][--bcnt[lv]];
            int i = c % NX, j = (c / NX) % NY, k = c / (NX * NY);
            if (j > maxj) maxj = j;
            if (i == 0 || i == NX - 1 || k == 0 || k == NZ - 1 || j == NY - 1) { spill = lv; break; }
            if (vw_air(i-1,j,k)) VW_SP(i-1,j,k,lv);
            if (vw_air(i+1,j,k)) VW_SP(i+1,j,k,lv);
            if (vw_air(i,j,k-1)) VW_SP(i,j,k-1,lv);
            if (vw_air(i,j,k+1)) VW_SP(i,j,k+1,lv);
            if (vw_air(i,j-1,k)) VW_SP(i,j-1,k,lv);
            if (vw_air(i,j+1,k)) VW_SP(i,j+1,k,(j+1 > lv ? j+1 : lv));
        }
    }
    #undef VW_SP
    for (int b = 0; b < NY; b++) free(bkt[b]);
    free(bkt); free(bcnt); free(bcap); free(vis);
    if (spill < 0) spill = maxj + 1;   /* fully enclosed: fill the whole pocket */
    return spill;
}

/* Mark the basin cells water is allowed in: connected air below `topj`, from the seed. */
static void vw_mark_basin(int si, int sj, int sk, int topj) {
    if (!g_vw_basin || !vw_air(si, sj, sk)) return;
    size_t N = (size_t)V.nx * V.ny * V.nz;
    int *q = (int *)malloc(N * sizeof(int));
    if (!q) return;
    int head = 0, tail = 0, sc = vidx(si, sj, sk);
    if (g_vw_basin[sc]) { free(q); return; }
    g_vw_basin[sc] = 1; q[tail++] = sc;
    while (head < tail) {
        int c = q[head++];
        int i = c % V.nx, j = (c / V.nx) % V.ny, k = c / (V.nx * V.ny);
        int ni[6] = { i-1, i+1, i, i, i, i };
        int nj[6] = { j, j, j-1, j+1, j, j };
        int nk[6] = { k, k, k, k, k-1, k+1 };
        for (int d = 0; d < 6; d++) {
            int ii = ni[d], jj = nj[d], kk = nk[d];
            if (ii < 0 || jj < 0 || kk < 0 || ii >= V.nx || jj >= V.ny || kk >= V.nz) continue;
            if (jj >= topj) continue;                 /* stay below the fill level */
            if (!vw_air(ii, jj, kk)) continue;
            int n = vidx(ii, jj, kk);
            if (g_vw_basin[n]) continue;
            if (tail >= (int)N) break;
            g_vw_basin[n] = 1; q[tail++] = n;
        }
    }
    free(q);
}

static void vw_alloc(void) {
    if (!V.active || g_vw) return;
    size_t n = (size_t)V.nx * V.ny * V.nz;
    g_vw  = (float *)calloc(n, sizeof(float));
    g_vw2 = (float *)calloc(n, sizeof(float));
    g_vw_basin = (unsigned char *)calloc(n, 1);
    if (!g_vw || !g_vw2 || !g_vw_basin) { free(g_vw); free(g_vw2); free(g_vw_basin); g_vw = g_vw2 = NULL; g_vw_basin = NULL; }
}

/* Mass the LOWER cell should hold given the total in a lower+upper pair (pressure). */
static inline float vw_stable_b(float total) {
    if (total <= VW_MAXMASS) return VW_MAXMASS;
    else if (total < 2.0f * VW_MAXMASS + VW_MAXCOMPRESS)
        return (VW_MAXMASS * VW_MAXMASS + total * VW_MAXCOMPRESS) / (VW_MAXMASS + VW_MAXCOMPRESS);
    else return (total + VW_MAXCOMPRESS) * 0.5f;
}

/* Keep each spring's cell topped up (a steady source). */
static void vw_add_sources(void) {
    for (int s = 0; s < g_nvwsrc; s++) {
        int i = (int)lrintf((g_vwsrc[s].x + V.ws * 0.5f) / V.sx);
        int j = (int)lrintf((g_vwsrc[s].y - V.ymin) / V.sy);
        int k = (int)lrintf((g_vwsrc[s].z + V.ws * 0.5f) / V.sz);
        if (i < 0) i = 0; if (j < 0) j = 0; if (k < 0) k = 0;
        if (i > V.nx - 1) i = V.nx - 1; if (j > V.ny - 1) j = V.ny - 1; if (k > V.nz - 1) k = V.nz - 1;
        while (j < V.ny - 1 && !vw_air(i, j, k)) j++;   /* into the air pocket at/above the spring */
        if (!vw_air(i, j, k)) continue;
        /* Gentle steady spring: just keeps its cell full. The basin is filled to the brim
           on reflow and confined by the mask, so this only tops up (no bulge, flat lake). */
        int c = vidx(i, j, k);
        if (g_vw[c] < VW_MAXMASS) g_vw[c] = VW_MAXMASS;
        vw_box_add(i, j, k);
    }
}

/* One pressure-flow pass over the active box (+1 ring). Water advances <=1 cell/step
   at the frontier; the box then grows to follow it. */
static void vw_substep(void) {
    if (g_vw_i0 > g_vw_i1) return;
    int i0 = g_vw_i0 - 1, i1 = g_vw_i1 + 1, j0 = g_vw_j0 - 1, j1 = g_vw_j1 + 1, k0 = g_vw_k0 - 1, k1 = g_vw_k1 + 1;
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (k0 < 0) k0 = 0;
    if (i1 > V.nx - 1) i1 = V.nx - 1; if (j1 > V.ny - 1) j1 = V.ny - 1; if (k1 > V.nz - 1) k1 = V.nz - 1;
    /* new_mass := mass over the working box */
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++)
        { int c = vidx(i, j, k); g_vw2[c] = g_vw[c]; }

    const int hx[4] = { -1, 1, 0, 0 }, hz[4] = { 0, 0, -1, 1 };
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        if (!vw_air(i, j, k)) continue;
        int c = vidx(i, j, k);
        float remaining = g_vw[c];
        if (remaining < VW_MINMASS) continue;
        /* DOWN (gravity) */
        if (j > j0 && vw_air(i, j - 1, k)) {
            int b = vidx(i, j - 1, k);
            float flow = vw_stable_b(remaining + g_vw[b]) - g_vw[b];
            if (flow > VW_MINMASS) flow *= 0.5f;
            if (flow < 0) flow = 0;
            float cap = remaining < VW_MAXSPEED ? remaining : VW_MAXSPEED;
            if (flow > cap) flow = cap;
            g_vw2[c] -= flow; g_vw2[b] += flow; remaining -= flow;
        }
        if (remaining < VW_MINMASS) continue;
        /* 4 HORIZONTAL (spread / find level) */
        for (int d = 0; d < 4; d++) {
            int ii = i + hx[d], kk = k + hz[d];
            if (ii < i0 || ii > i1 || kk < k0 || kk > k1) continue;
            if (!vw_air(ii, j, kk)) continue;
            int nn = vidx(ii, j, kk);
            float flow = (remaining - g_vw[nn]) * 0.2f;   /* /5 (self + 4 neighbours) */
            if (flow > VW_MINMASS) flow *= 0.5f;
            if (flow < 0) flow = 0;
            if (flow > remaining) flow = remaining;
            g_vw2[c] -= flow; g_vw2[nn] += flow; remaining -= flow;
            if (remaining < VW_MINMASS) break;
        }
        if (remaining < VW_MINMASS) continue;
        /* UP (pressure pushes water up to communicate levels) */
        if (j < j1 && vw_air(i, j + 1, k)) {
            int a = vidx(i, j + 1, k);
            float flow = remaining - vw_stable_b(remaining + g_vw[a]);
            if (flow > VW_MINMASS) flow *= 0.5f;
            if (flow < 0) flow = 0;
            float cap = remaining < VW_MAXSPEED ? remaining : VW_MAXSPEED;
            if (flow > cap) flow = cap;
            g_vw2[c] -= flow; g_vw2[a] += flow; remaining -= flow;
        }
    }
    /* commit + recompute the tight wet box. Water reaching the world edge or the
       bottom drains off the map, so a spring reaches steady state (river/lake) instead
       of flooding everything. */
    g_vw_i0 = 1; g_vw_i1 = 0;   /* empty */
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        int c = vidx(i, j, k);
        float m = g_vw2[c];
        if (m < 0.5f) m -= VW_EVAP;                  /* only THIN water evaporates (keeps the full
                                                        lake intact; trims stray shallow film) */
        if (m < 0.0f) m = 0.0f;
        if (i == 0 || i == V.nx - 1 || k == 0 || k == V.nz - 1 || j == 0) m = 0.0f;  /* drain off-map */
        if (g_vw_basin && !g_vw_basin[c]) m = 0.0f; /* confine to the basin -> nothing escapes */
        g_vw[c] = m;
        if (m >= VW_MINMASS) vw_box_add(i, j, k);
    }
    for (int s = 0; s < g_nvwsrc; s++) {   /* keep sources inside the box even before they fill */
        int i = (int)lrintf((g_vwsrc[s].x + V.ws * 0.5f) / V.sx);
        int j = (int)lrintf((g_vwsrc[s].y - V.ymin) / V.sy);
        int k = (int)lrintf((g_vwsrc[s].z + V.ws * 0.5f) / V.sz);
        if (i >= 0 && j >= 0 && k >= 0 && i < V.nx && j < V.ny && k < V.nz) vw_box_add(i, j, k);
    }
    g_vw_dirty = 1;
}

/* Recompute every spring's basin (call after terrain changes): dig a breach in a wall and
   the basin now reaches through it, so the water flows out; wall it back and it contains
   again. Water sitting outside the new basins is dropped so the level follows the terrain. */
void g3d_voxwater_reflow(void) {
    if (!V.active || !g_vw_basin) return;
    memset(g_vw_basin, 0, (size_t)V.nx * V.ny * V.nz);
    for (int s = 0; s < g_nvwsrc; s++) {
        int i = (int)lrintf((g_vwsrc[s].x + V.ws * 0.5f) / V.sx);
        int j = (int)lrintf((g_vwsrc[s].y - V.ymin) / V.sy);
        int k = (int)lrintf((g_vwsrc[s].z + V.ws * 0.5f) / V.sz);
        if (i < 0) i = 0; if (j < 0) j = 0; if (k < 0) k = 0;
        if (i > V.nx - 1) i = V.nx - 1; if (j > V.ny - 1) j = V.ny - 1; if (k > V.nz - 1) k = V.nz - 1;
        while (j < V.ny - 1 && !vw_air(i, j, k)) j++;
        if (!vw_air(i, j, k)) continue;
        int spill = vw_spill_level(i, j, k);
        int topj = spill - VW_MARGIN; if (topj <= j) topj = j + 1;   /* keep the water below the brim */
        vw_mark_basin(i, j, k, topj);
    }
    if (g_vw) {  /* fill the basins to the brim at once (one click = full lake) and drop
                    any water now outside them (e.g. after a breach) */
        g_vw_i0 = 1; g_vw_i1 = 0;
        for (int k = 0; k < V.nz; k++) for (int j = 0; j < V.ny; j++) for (int i = 0; i < V.nx; i++) {
            int c = vidx(i, j, k);
            if (g_vw_basin[c]) { g_vw[c] = VW_MAXMASS; vw_box_add(i, j, k); }
            else g_vw[c] = 0.0f;
        }
    }
    g_vw_dirty = 1;
}

int g3d_voxwater_add_source(float wx, float wy, float wz, float rate) {
    if (!V.active || g_nvwsrc >= 256) return -1;
    vw_alloc(); if (!g_vw) return -1;
    g_vwsrc[g_nvwsrc].x = wx; g_vwsrc[g_nvwsrc].y = wy; g_vwsrc[g_nvwsrc].z = wz; g_vwsrc[g_nvwsrc].r = rate;
    g_nvwsrc++;
    g3d_voxwater_reflow();  /* (re)compute the basin this spring can fill */
    vw_add_sources();       /* seed immediately so it appears without a settle */
    g_vw_dirty = 1;
    return g_nvwsrc - 1;
}
void g3d_voxwater_clear_sources(void) {
    g_nvwsrc = 0;
    if (g_vw_basin) memset(g_vw_basin, 0, (size_t)V.nx * V.ny * V.nz);
}
int  g3d_voxwater_source_count(void) { return g_nvwsrc; }
int  g3d_voxwater_get_source(int n, float *x, float *y, float *z, float *rate) {
    if (n < 0 || n >= g_nvwsrc) return 0;
    if (x) *x = g_vwsrc[n].x; if (y) *y = g_vwsrc[n].y; if (z) *z = g_vwsrc[n].z; if (rate) *rate = g_vwsrc[n].r;
    return 1;
}

void g3d_voxwater_step(float dt) {
    (void)dt;
    if (!g3d_voxwater_active()) return;
    vw_add_sources();
    vw_substep();
    vw_substep();
}

void g3d_voxwater_settle(float seconds) {
    if (!g3d_voxwater_active()) return;
    int steps = (int)(seconds * 40.0f);
    if (steps < 40) steps = 40; if (steps > 4000) steps = 4000;
    for (int s = 0; s < steps; s++) { vw_add_sources(); vw_substep(); }
}

/* Smooth water surface via MARCHING CUBES on the water-mass field (same technique as
   the terrain): the water VOLUME is wrapped in one continuous rounded isosurface, so a
   stream/pool/cave-fill reads as real fluid, not stacked plates. Bounded to the wet box. */
#define VW_ISO 0.06f   /* isolevel: low, so thin flow still connects the surface */

static inline float vwm(int i, int j, int k) {
    if (i < 0) i = 0; else if (i >= V.nx) i = V.nx - 1;
    if (j < 0) j = 0; else if (j >= V.ny) j = V.ny - 1;
    if (k < 0) k = 0; else if (k >= V.nz) k = V.nz - 1;
    return g_vw[vidx(i, j, k)];
}
static void vwm_grad(int i, int j, int k, float *gx, float *gy, float *gz) {
    *gx = vwm(i + 1, j, k) - vwm(i - 1, j, k);
    *gy = vwm(i, j + 1, k) - vwm(i, j - 1, k);
    *gz = vwm(i, j, k + 1) - vwm(i, j, k - 1);
}

static void vw_build_mesh(void) {
    if (g_vw_mesh) { g3d_mesh_free(g_vw_mesh); g_vw_mesh = NULL; }
    if (!g_vw || g_vw_i0 > g_vw_i1) return;
    int i0 = g_vw_i0 - 1, i1 = g_vw_i1 + 1, j0 = g_vw_j0 - 1, j1 = g_vw_j1 + 1, k0 = g_vw_k0 - 1, k1 = g_vw_k1 + 1;
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (k0 < 0) k0 = 0;
    if (i1 > V.nx - 2) i1 = V.nx - 2; if (j1 > V.ny - 2) j1 = V.ny - 2; if (k1 > V.nz - 2) k1 = V.nz - 2;
    if (i1 < i0 || j1 < j0 || k1 < k0) return;

    int cap = 8192, nv = 0;
    G3DVertex *verts = (G3DVertex *)malloc((size_t)cap * sizeof(G3DVertex));
    if (!verts) return;

    for (int z = k0; z <= k1; z++) for (int y = j0; y <= j1; y++) for (int x = i0; x <= i1; x++) {
        float val[8], px[8], py[8], pz[8], gx8[8], gy8[8], gz8[8];
        int cubeindex = 0;
        for (int c = 0; c < 8; c++) {
            int ii = x + cornerOff[c][0], jj = y + cornerOff[c][1], kk = z + cornerOff[c][2];
            val[c] = vwm(ii, jj, kk);
            px[c] = wx_of(ii); py[c] = wy_of(jj); pz[c] = wz_of(kk);
            vwm_grad(ii, jj, kk, &gx8[c], &gy8[c], &gz8[c]);
            if (val[c] > VW_ISO) cubeindex |= (1 << c);   /* inside water */
        }
        int edges = edgeTable[cubeindex];
        if (edges == 0) continue;
        float ex[12], ey[12], ez[12], enx[12], eny[12], enz[12];
        for (int e = 0; e < 12; e++) {
            if (!(edges & (1 << e))) continue;
            int a = edgeCorner[e][0], b = edgeCorner[e][1];
            float denom = (val[b] - val[a]);
            float t = (denom == 0.0f) ? 0.5f : (VW_ISO - val[a]) / denom;
            ex[e] = px[a] + t * (px[b] - px[a]);
            ey[e] = py[a] + t * (py[b] - py[a]);
            ez[e] = pz[a] + t * (pz[b] - pz[a]);
            float gx = gx8[a] + t * (gx8[b] - gx8[a]);
            float gy = gy8[a] + t * (gy8[b] - gy8[a]);
            float gz = gz8[a] + t * (gz8[b] - gz8[a]);
            float gl = sqrtf(gx * gx + gy * gy + gz * gz); if (gl < 1e-6f) gl = 1.0f;
            enx[e] = -gx / gl; eny[e] = -gy / gl; enz[e] = -gz / gl;   /* -grad -> outward */
        }
        for (int t = 0; triTable[cubeindex][t] != -1; t += 3) {
            if (nv + 3 > cap) { cap *= 2; verts = (G3DVertex *)realloc(verts, (size_t)cap * sizeof(G3DVertex)); }
            for (int w = 0; w < 3; w++) {
                int e = triTable[cubeindex][t + w];
                G3DVertex *Vt = &verts[nv++];
                Vt->position[0] = ex[e]; Vt->position[1] = ey[e]; Vt->position[2] = ez[e];
                Vt->normal[0] = enx[e]; Vt->normal[1] = eny[e]; Vt->normal[2] = enz[e];
                Vt->texcoord[0] = 3.0f; Vt->texcoord[1] = 0.0f;
            }
        }
    }
    if (nv > 0) {
        uint32_t *idx = (uint32_t *)malloc((size_t)nv * sizeof(uint32_t));
        for (int i = 0; i < nv; i++) idx[i] = (uint32_t)i;
        g_vw_mesh = g3d_mesh_create("voxwater", verts, (uint32_t)nv, idx, (uint32_t)nv);
        free(idx);
        if (g_vw_mesh) g3d_mesh_upload_gpu(g_vw_mesh);
    }
    free(verts);
}

void g3d_voxwater_render(G3DCamera *camera, int flip_y) {
    if (!g3d_voxwater_active() || !camera) return;
    if (g_vw_dirty) { vw_build_mesh(); g_vw_dirty = 0; }
    if (g_vw_mesh) g3d_water_draw_mesh(g_vw_mesh, camera, flip_y);
}
