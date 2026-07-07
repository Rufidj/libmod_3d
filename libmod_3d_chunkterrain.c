/*
 * libmod_3d_chunkterrain.c - Chunked terrain from a shared height array.
 */

#include "libmod_3d_chunkterrain.h"
#include "libmod_3d_entity.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- shared height array helpers --------------------------------------- */

float g3d_heightfield_height(const float *H, int side, float ws,
                             float wx, float wz) {
    if (!H || side < 2) return 0.0f;
    int grid = side - 1;
    float gx = (wx / ws + 0.5f) * (float)grid;
    float gz = (wz / ws + 0.5f) * (float)grid;
    if (gx < 0 || gz < 0 || gx > grid || gz > grid) return 0.0f;
    int ix = (int)gx, iz = (int)gz;
    int ix1 = ix < grid ? ix + 1 : ix;
    int iz1 = iz < grid ? iz + 1 : iz;
    float fx = gx - ix, fz = gz - iz;
    float h00 = H[iz * side + ix], h10 = H[iz * side + ix1];
    float h01 = H[iz1 * side + ix], h11 = H[iz1 * side + ix1];
    float a = h00 + (h10 - h00) * fx;
    float b = h01 + (h11 - h01) * fx;
    return a + (b - a) * fz;
}

/* Smooth radial falloff (1 at centre -> 0 at radius). */
static float falloff(float d, float r) {
    if (r <= 0.0f || d >= r) return 0.0f;
    float t = 1.0f - d / r;
    return t * t * (3.0f - 2.0f * t);
}

/* Grid-index window covering the world disc (wx,wz,radius). */
static void disc_window(int side, float ws, float wx, float wz, float radius,
                        int *i0, int *i1, int *j0, int *j1) {
    int grid = side - 1;
    float cell = ws / (float)grid;
    float gcx = (wx / ws + 0.5f) * grid, gcz = (wz / ws + 0.5f) * grid;
    int gr = (int)(radius / cell) + 1;
    *i0 = (int)gcx - gr; *i1 = (int)gcx + gr;
    *j0 = (int)gcz - gr; *j1 = (int)gcz + gr;
    if (*i0 < 0) *i0 = 0; if (*j0 < 0) *j0 = 0;
    if (*i1 > grid) *i1 = grid; if (*j1 > grid) *j1 = grid;
}

void g3d_heightfield_raise(float *H, int side, float ws,
                           float wx, float wz, float radius, float strength) {
    if (!H || side < 2) return;
    int grid = side - 1;
    float cell = ws / (float)grid;
    int i0, i1, j0, j1; disc_window(side, ws, wx, wz, radius, &i0, &i1, &j0, &j1);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float pwx = ((float)i / grid - 0.5f) * ws;
            float pwz = ((float)j / grid - 0.5f) * ws;
            float d = sqrtf((pwx - wx) * (pwx - wx) + (pwz - wz) * (pwz - wz));
            H[j * side + i] += strength * falloff(d, radius);
        }
    (void)cell;
}

void g3d_heightfield_smooth(float *H, int side, float ws,
                            float wx, float wz, float radius, float amount) {
    if (!H || side < 2) return;
    int grid = side - 1;
    int i0, i1, j0, j1; disc_window(side, ws, wx, wz, radius, &i0, &i1, &j0, &j1);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            float pwx = ((float)i / grid - 0.5f) * ws;
            float pwz = ((float)j / grid - 0.5f) * ws;
            float d = sqrtf((pwx - wx) * (pwx - wx) + (pwz - wz) * (pwz - wz));
            float f = falloff(d, radius);
            if (f <= 0.0f) continue;
            int il = i > 0 ? i - 1 : i, ir = i < grid ? i + 1 : i;
            int jd = j > 0 ? j - 1 : j, ju = j < grid ? j + 1 : j;
            float avg = 0.25f * (H[j * side + il] + H[j * side + ir] +
                                 H[jd * side + i] + H[ju * side + i]);
            float h = H[j * side + i];
            H[j * side + i] = h + (avg - h) * amount * f;
        }
}

/* ---- chunk mesh build -------------------------------------------------- */

/* Fill verts/idx for chunk (cx,cz). Vertices are GLOBAL-positioned (world),
   texcoords global, normals sampled from global heights (so seams match). */
static void build_chunk(const float *H, int side, float ws, int chunks,
                        float tiling, int cx, int cz,
                        G3DVertex *verts, uint32_t *idx,
                        const unsigned char *holes) {
    int grid = side - 1;
    int cw = grid / chunks;          /* cells per chunk */
    int cs = cw + 1;                 /* verts per chunk side */
    int i0 = cx * cw, j0 = cz * cw;  /* global vertex offset */
    float cell = ws / (float)grid;

    for (int lj = 0; lj < cs; lj++)
        for (int li = 0; li < cs; li++) {
            int gi = i0 + li, gj = j0 + lj;
            int lidx = lj * cs + li;
            int il = gi > 0 ? gi - 1 : gi, ir = gi < grid ? gi + 1 : gi;
            int jd = gj > 0 ? gj - 1 : gj, ju = gj < grid ? gj + 1 : gj;
            float hl = H[gj * side + il], hr = H[gj * side + ir];
            float hd = H[jd * side + gi], hu = H[ju * side + gi];
            float nx = hl - hr, ny = 2.0f * cell, nz = hd - hu;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len < 1e-6f) len = 1.0f;
            verts[lidx].position[0] = ((float)gi / grid - 0.5f) * ws;
            verts[lidx].position[1] = H[gj * side + gi];
            verts[lidx].position[2] = ((float)gj / grid - 0.5f) * ws;
            verts[lidx].normal[0] = nx / len;
            verts[lidx].normal[1] = ny / len;
            verts[lidx].normal[2] = nz / len;
            verts[lidx].texcoord[0] = (float)gi / grid * tiling;
            verts[lidx].texcoord[1] = (float)gj / grid * tiling;
        }

    int k = 0;
    for (int lj = 0; lj < cw; lj++)
        for (int li = 0; li < cw; li++) {
            uint32_t a = (uint32_t)(lj * cs + li);
            uint32_t b = (uint32_t)(lj * cs + li + 1);
            uint32_t c = (uint32_t)((lj + 1) * cs + li);
            uint32_t d = (uint32_t)((lj + 1) * cs + li + 1);
            /* holed cell -> degenerate triangles (zero area, not drawn): punches a
               hole in the terrain for a cave entrance without changing buffer sizes */
            int gi = i0 + li, gj = j0 + lj;
            if (holes && holes[gj * side + gi]) { a = b = c = d = 0; }
            idx[k++] = a; idx[k++] = c; idx[k++] = b;
            idx[k++] = b; idx[k++] = c; idx[k++] = d;
        }
}

int g3d_chunkterrain_build(const float *H, int side, float ws, int chunks,
                           float tiling, int scene, int material,
                           G3DMesh **out_meshes, int *out_entities,
                           const unsigned char *holes) {
    if (!H || side < 2 || chunks < 1) return 0;
    int grid = side - 1;
    if (grid % chunks != 0) return 0;
    int cw = grid / chunks, cs = cw + 1;
    int vcount = cs * cs, icount = cw * cw * 6;

    G3DVertex *verts = (G3DVertex *)malloc((size_t)vcount * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!verts || !idx) { free(verts); free(idx); return 0; }

    for (int cz = 0; cz < chunks; cz++)
        for (int cx = 0; cx < chunks; cx++) {
            build_chunk(H, side, ws, chunks, tiling, cx, cz, verts, idx, holes);
            G3DMesh *m = g3d_mesh_create("chunk", verts, (uint32_t)vcount,
                                         idx, (uint32_t)icount);
            g3d_mesh_upload_gpu(m);
            int ci = cz * chunks + cx;
            if (out_meshes) out_meshes[ci] = m;
            int e = g3d_entity_impl_spawn(scene, 0, 0, 0, 0);
            G3DEntity *ge = g3d_entity_impl_get(e);
            if (ge) { ge->mesh = m; ge->material_id = material; }
            if (out_entities) out_entities[ci] = e;
        }
    free(verts);
    free(idx);
    return chunks * chunks;
}

void g3d_chunkterrain_refresh(const float *H, int side, float ws, int chunks,
                              float tiling, G3DMesh **meshes,
                              float minx, float minz, float maxx, float maxz,
                              const unsigned char *holes) {
    if (!H || side < 2 || chunks < 1 || !meshes) return;
    int grid = side - 1;
    if (grid % chunks != 0) return;
    int cw = grid / chunks, cs = cw + 1;
    int vcount = cs * cs, icount = cw * cw * 6;
    float margin = ws / (float)grid;   /* a cell, so seam neighbours refresh too */
    minx -= margin; minz -= margin; maxx += margin; maxz += margin;

    G3DVertex *verts = (G3DVertex *)malloc((size_t)vcount * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)icount * sizeof(uint32_t));
    if (!verts || !idx) { free(verts); free(idx); return; }

    for (int cz = 0; cz < chunks; cz++)
        for (int cx = 0; cx < chunks; cx++) {
            float cx0 = ((float)(cx * cw) / grid - 0.5f) * ws;
            float cx1 = ((float)(cx * cw + cw) / grid - 0.5f) * ws;
            float cz0 = ((float)(cz * cw) / grid - 0.5f) * ws;
            float cz1 = ((float)(cz * cw + cw) / grid - 0.5f) * ws;
            if (cx1 < minx || cx0 > maxx || cz1 < minz || cz0 > maxz) continue;
            int ci = cz * chunks + cx;
            if (!meshes[ci] || !meshes[ci]->vertices) continue;
            build_chunk(H, side, ws, chunks, tiling, cx, cz, verts, idx, holes);
            memcpy(meshes[ci]->vertices, verts, (size_t)vcount * sizeof(G3DVertex));
            if (meshes[ci]->indices)
                memcpy(meshes[ci]->indices, idx, (size_t)icount * sizeof(uint32_t));
            g3d_mesh_calculate_bounds(meshes[ci]);
            g3d_mesh_update_gpu(meshes[ci]);
            g3d_mesh_update_indices_gpu(meshes[ci]);
        }
    free(verts);
    free(idx);
}

/* ---- .g3dh file -------------------------------------------------------- */

#define G3DH_MAGIC "G3DH"

int g3d_heightmap_write(const char *path, const float *H, int side,
                        float ws, float tiling, int chunks) {
    if (!path || !H || side < 2) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int32_t s = side, c = chunks < 1 ? 1 : chunks;
    fwrite(G3DH_MAGIC, 1, 4, f);
    fwrite(&s, sizeof(int32_t), 1, f);
    fwrite(&ws, sizeof(float), 1, f);
    fwrite(&tiling, sizeof(float), 1, f);
    fwrite(&c, sizeof(int32_t), 1, f);
    fwrite(H, sizeof(float), (size_t)side * side, f);
    fclose(f);
    return 1;
}

float *g3d_heightmap_read(const char *path, int *side_out, float *ws_out,
                          float *tiling_out, int *chunks_out) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[4];
    int32_t side = 0, chunks = 1;
    float ws = 0.0f, tiling = 1.0f;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, G3DH_MAGIC, 4) != 0 ||
        fread(&side, sizeof(int32_t), 1, f) != 1 ||
        fread(&ws, sizeof(float), 1, f) != 1 ||
        fread(&tiling, sizeof(float), 1, f) != 1 ||
        fread(&chunks, sizeof(int32_t), 1, f) != 1 || side < 2) {
        fclose(f); return NULL;
    }
    int n = side * side;
    float *H = (float *)malloc((size_t)n * sizeof(float));
    if (!H || fread(H, sizeof(float), (size_t)n, f) != (size_t)n) {
        free(H); fclose(f); return NULL;
    }
    fclose(f);
    if (side_out) *side_out = side;
    if (ws_out) *ws_out = ws;
    if (tiling_out) *tiling_out = tiling;
    if (chunks_out) *chunks_out = chunks < 1 ? 1 : chunks;
    return H;
}
