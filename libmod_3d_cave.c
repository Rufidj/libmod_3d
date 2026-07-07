/*
 * libmod_3d_cave.c - Voxel caves via marching cubes (hybrid with heightfield).
 *
 * Each cave is a res^3 signed density field: dens > 0 = solid rock, dens < 0 = air.
 * The user carves with an additive 3D sphere brush; the isosurface (dens = 0) is
 * extracted with marching cubes into a lit/collidable mesh. Surface terrain stays
 * a heightfield, so nothing else has to change.
 */

#include "libmod_3d_cave.h"
#include "libmod_3d_chunkterrain.h"   /* g3d_heightfield_height */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define CAVE_MAX 64

typedef struct {
    int used;
    float cx, cy, cz, size;
    int res;
    float step;              /* world units per voxel = size/(res-1) */
    float *dens;             /* res^3, >0 solid, <0 air */
    G3DMesh *mesh;
} Cave;

static Cave g_caves[CAVE_MAX];
static int g_cave_count = 0;

#include "libmod_3d_mctables.h"

/* ---- volume helpers ---------------------------------------------------- */

static inline float dens_at(Cave *c, int i, int j, int k) {
    if (i < 0) i = 0; if (j < 0) j = 0; if (k < 0) k = 0;
    if (i >= c->res) i = c->res - 1;
    if (j >= c->res) j = c->res - 1;
    if (k >= c->res) k = c->res - 1;
    return c->dens[(k * c->res + j) * c->res + i];
}

int g3d_cave_create(float cx, float cy, float cz, float size, int res) {
    if (g_cave_count >= CAVE_MAX) return -1;
    if (res < 8) res = 8; if (res > 96) res = 96;
    if (size < 1.0f) size = 1.0f;
    int id = -1;
    for (int i = 0; i < CAVE_MAX; i++) if (!g_caves[i].used) { id = i; break; }
    if (id < 0) return -1;
    Cave *c = &g_caves[id];
    memset(c, 0, sizeof(*c));
    c->used = 1; c->cx = cx; c->cy = cy; c->cz = cz; c->size = size; c->res = res;
    c->step = size / (float)(res - 1);
    size_t n = (size_t)res * res * res;
    c->dens = (float *)malloc(n * sizeof(float));
    for (size_t i = 0; i < n; i++) c->dens[i] = 1.0f;   /* all solid rock */
    if (id + 1 > g_cave_count) g_cave_count = id + 1;
    return id;
}

void g3d_cave_clear(void) {
    for (int i = 0; i < CAVE_MAX; i++) {
        if (g_caves[i].dens) free(g_caves[i].dens);
        if (g_caves[i].mesh) g3d_mesh_free(g_caves[i].mesh);
        memset(&g_caves[i], 0, sizeof(Cave));
    }
    g_cave_count = 0;
}

int g3d_cave_count(void) { return g_cave_count; }

void g3d_cave_fill(int id, float value) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return;
    Cave *c = &g_caves[id];
    size_t n = (size_t)c->res * c->res * c->res;
    for (size_t i = 0; i < n; i++) c->dens[i] = value;
}

void g3d_cave_fill_from_heightfield(int id, const float *H, int side, float ws) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used || !H) return;
    Cave *c = &g_caves[id];
    float ox = c->cx - c->size * 0.5f, oy = c->cy - c->size * 0.5f, oz = c->cz - c->size * 0.5f;
    int R = c->res;
    for (int k = 0; k < R; k++) for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
        float wx = ox + i * c->step, wy = oy + j * c->step, wz = oz + k * c->step;
        float th = g3d_heightfield_height(H, side, ws, wx, wz);
        float d = th - wy;                         /* >0 below ground (solid), <0 above (air) */
        if (d > 2.0f) d = 2.0f; if (d < -2.0f) d = -2.0f;
        c->dens[(k * R + j) * R + i] = d;
    }
}

/* trilinear density at a world point (clamped to the volume) */
static float dens_sample(Cave *c, float wx, float wy, float wz) {
    float ox = c->cx - c->size * 0.5f, oy = c->cy - c->size * 0.5f, oz = c->cz - c->size * 0.5f;
    float fx = (wx - ox) / c->step, fy = (wy - oy) / c->step, fz = (wz - oz) / c->step;
    int i = (int)floorf(fx), j = (int)floorf(fy), k = (int)floorf(fz);
    float tx = fx - i, ty = fy - j, tz = fz - k;
    float c00 = dens_at(c, i, j, k)     * (1 - tx) + dens_at(c, i + 1, j, k)     * tx;
    float c10 = dens_at(c, i, j + 1, k) * (1 - tx) + dens_at(c, i + 1, j + 1, k) * tx;
    float c01 = dens_at(c, i, j, k + 1) * (1 - tx) + dens_at(c, i + 1, j, k + 1) * tx;
    float c11 = dens_at(c, i, j + 1, k + 1) * (1 - tx) + dens_at(c, i + 1, j + 1, k + 1) * tx;
    float c0 = c00 * (1 - ty) + c10 * ty;
    float c1 = c01 * (1 - ty) + c11 * ty;
    return c0 * (1 - tz) + c1 * tz;
}

int g3d_cave_raycast(int id, float ox, float oy, float oz, float dx, float dy, float dz,
                     float maxdist, float *hx, float *hy, float *hz) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return 0;
    Cave *c = &g_caves[id];
    float dl = sqrtf(dx * dx + dy * dy + dz * dz); if (dl < 1e-6f) return 0;
    dx /= dl; dy /= dl; dz /= dl;
    float step = c->step * 0.4f;
    float prev = dens_sample(c, ox, oy, oz);   /* <0 air, >0 solid */
    for (float t = step; t <= maxdist; t += step) {
        float x = ox + dx * t, y = oy + dy * t, z = oz + dz * t;
        float d = dens_sample(c, x, y, z);
        if (prev < 0.0f && d >= 0.0f) {          /* air -> solid: rock surface */
            if (hx) *hx = x; if (hy) *hy = y; if (hz) *hz = z;
            return 1;
        }
        prev = d;
    }
    return 0;
}

void g3d_cave_bounds(int id, float *cx, float *cy, float *cz, float *size) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return;
    Cave *c = &g_caves[id];
    if (cx) *cx = c->cx; if (cy) *cy = c->cy; if (cz) *cz = c->cz; if (size) *size = c->size;
}

int g3d_cave_edit(int id, float wx, float wy, float wz, float radius, int mode, float strength) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return 0;
    Cave *c = &g_caves[id];
    float ox = c->cx - c->size * 0.5f, oy = c->cy - c->size * 0.5f, oz = c->cz - c->size * 0.5f;
    /* voxel-space bounding box of the brush sphere */
    int i0 = (int)floorf((wx - radius - ox) / c->step), i1 = (int)ceilf((wx + radius - ox) / c->step);
    int j0 = (int)floorf((wy - radius - oy) / c->step), j1 = (int)ceilf((wy + radius - oy) / c->step);
    int k0 = (int)floorf((wz - radius - oz) / c->step), k1 = (int)ceilf((wz + radius - oz) / c->step);
    if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0; if (k0 < 0) k0 = 0;
    if (i1 >= c->res) i1 = c->res - 1; if (j1 >= c->res) j1 = c->res - 1; if (k1 >= c->res) k1 = c->res - 1;
    if (strength <= 0.0f) strength = 1.0f;
    int changed = 0;
    for (int k = k0; k <= k1; k++) for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        float vx = ox + i * c->step, vy = oy + j * c->step, vz = oz + k * c->step;
        float dx = vx - wx, dy = vy - wy, dz = vz - wz;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > radius) continue;
        float fall = (radius - d) / radius;          /* 1 at centre -> 0 at rim */
        float delta = strength * fall * 2.0f;
        float *cell = &c->dens[(k * c->res + j) * c->res + i];
        *cell += (mode == 0) ? -delta : delta;        /* carve air / add rock */
        if (*cell > 2.0f) *cell = 2.0f; if (*cell < -2.0f) *cell = -2.0f;
        changed = 1;
    }
    return changed;
}

static void cave_gradient(Cave *c, int i, int j, int k, float *gx, float *gy, float *gz) {
    *gx = dens_at(c, i + 1, j, k) - dens_at(c, i - 1, j, k);
    *gy = dens_at(c, i, j + 1, k) - dens_at(c, i, j - 1, k);
    *gz = dens_at(c, i, j, k + 1) - dens_at(c, i, j, k - 1);
}

G3DMesh *g3d_cave_build_mesh(int id) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return NULL;
    Cave *c = &g_caves[id];
    if (c->mesh) { g3d_mesh_free(c->mesh); c->mesh = NULL; }
    int R = c->res;
    float ox = c->cx - c->size * 0.5f, oy = c->cy - c->size * 0.5f, oz = c->cz - c->size * 0.5f;

    int cap = 4096, nv = 0;
    G3DVertex *verts = (G3DVertex *)malloc((size_t)cap * sizeof(G3DVertex));

    for (int k = 0; k < R - 1; k++)
    for (int j = 0; j < R - 1; j++)
    for (int i = 0; i < R - 1; i++) {
        float val[8]; float px[8], py[8], pz[8]; float nx[8], ny[8], nz[8];
        int cubeindex = 0;
        for (int ci = 0; ci < 8; ci++) {
            int ii = i + cornerOff[ci][0], jj = j + cornerOff[ci][1], kk = k + cornerOff[ci][2];
            val[ci] = c->dens[(kk * R + jj) * R + ii];
            px[ci] = ox + ii * c->step; py[ci] = oy + jj * c->step; pz[ci] = oz + kk * c->step;
            cave_gradient(c, ii, jj, kk, &nx[ci], &ny[ci], &nz[ci]);
            if (val[ci] > 0.0f) cubeindex |= (1 << ci);   /* solid "inside" -> faces toward the air/cave */
        }
        int edges = edgeTable[cubeindex];
        if (edges == 0) continue;
        float ex[12], ey[12], ez[12], enx[12], eny[12], enz[12];
        for (int e = 0; e < 12; e++) {
            if (!(edges & (1 << e))) continue;
            int a = edgeCorner[e][0], b = edgeCorner[e][1];
            float t = (0.0f - val[a]) / (val[b] - val[a]);
            if (val[b] == val[a]) t = 0.5f;
            ex[e] = px[a] + t * (px[b] - px[a]);
            ey[e] = py[a] + t * (py[b] - py[a]);
            ez[e] = pz[a] + t * (pz[b] - pz[a]);
            /* normal from the interpolated density gradient, pointing into the air */
            float gx = nx[a] + t * (nx[b] - nx[a]);
            float gy = ny[a] + t * (ny[b] - ny[a]);
            float gz = nz[a] + t * (nz[b] - nz[a]);
            float gl = sqrtf(gx * gx + gy * gy + gz * gz); if (gl < 1e-6f) gl = 1.0f;
            enx[e] = -gx / gl; eny[e] = -gy / gl; enz[e] = -gz / gl;
        }
        for (int t = 0; triTable[cubeindex][t] != -1; t += 3) {
            if (nv + 3 > cap) { cap *= 2; verts = (G3DVertex *)realloc(verts, (size_t)cap * sizeof(G3DVertex)); }
            for (int w = 0; w < 3; w++) {
                int e = triTable[cubeindex][t + w];
                G3DVertex *V = &verts[nv++];
                V->position[0] = ex[e]; V->position[1] = ey[e]; V->position[2] = ez[e];
                V->normal[0] = enx[e]; V->normal[1] = eny[e]; V->normal[2] = enz[e];
                /* planar UV from world XZ (a rock texture tiles reasonably) */
                V->texcoord[0] = ex[e] * 0.25f; V->texcoord[1] = ez[e] * 0.25f;
            }
        }
    }

    if (nv == 0) { free(verts); return NULL; }
    uint32_t *idx = (uint32_t *)malloc((size_t)nv * sizeof(uint32_t));
    for (int i = 0; i < nv; i++) idx[i] = (uint32_t)i;
    c->mesh = g3d_mesh_create("cave", verts, (uint32_t)nv, idx, (uint32_t)nv);
    free(verts); free(idx);
    if (c->mesh) g3d_mesh_upload_gpu(c->mesh);
    return c->mesh;
}

G3DMesh *g3d_cave_mesh(int id) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used) return NULL;
    return g_caves[id].mesh;
}

/* ---- persistence: raw density volume ----------------------------------- */
#define CAVE_MAGIC 0x43563031  /* "CV01" */

int g3d_cave_save(int id, const char *path) {
    if (id < 0 || id >= CAVE_MAX || !g_caves[id].used || !path) return 0;
    Cave *c = &g_caves[id];
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    int magic = CAVE_MAGIC;
    fwrite(&magic, sizeof(int), 1, f);
    fwrite(&c->cx, sizeof(float), 1, f); fwrite(&c->cy, sizeof(float), 1, f);
    fwrite(&c->cz, sizeof(float), 1, f); fwrite(&c->size, sizeof(float), 1, f);
    fwrite(&c->res, sizeof(int), 1, f);
    fwrite(c->dens, sizeof(float), (size_t)c->res * c->res * c->res, f);
    fclose(f);
    return 1;
}

int g3d_cave_load(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int magic = 0; if (fread(&magic, sizeof(int), 1, f) != 1 || magic != CAVE_MAGIC) { fclose(f); return -1; }
    float cx, cy, cz, size; int res;
    if (fread(&cx, sizeof(float), 1, f) != 1 || fread(&cy, sizeof(float), 1, f) != 1 ||
        fread(&cz, sizeof(float), 1, f) != 1 || fread(&size, sizeof(float), 1, f) != 1 ||
        fread(&res, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
    int id = g3d_cave_create(cx, cy, cz, size, res);
    if (id < 0) { fclose(f); return -1; }
    Cave *c = &g_caves[id];
    if (fread(c->dens, sizeof(float), (size_t)res * res * res, f) != (size_t)res * res * res) {
        fclose(f); return id;   /* partial, but keep what we got */
    }
    fclose(f);
    g3d_cave_build_mesh(id);
    return id;
}
