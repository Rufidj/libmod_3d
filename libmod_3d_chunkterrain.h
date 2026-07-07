/*
 * libmod_3d_chunkterrain.h - Chunked terrain (for large / kilometric worlds)
 *
 * A terrain is one shared (side x side) height array, but it is rendered as a
 * grid of `chunks x chunks` separate meshes/entities so the renderer can frustum
 * -cull the chunks you can't see. Vertices carry GLOBAL positions and texcoords
 * and normals are sampled from the global heights, so chunk borders are seamless
 * (geometry AND lighting) and a single paint canvas spans the whole terrain.
 *
 * The height array is the source of truth: sculpt it (g3d_heightfield_*) and call
 * g3d_chunkterrain_refresh() to rebuild just the chunks under the brush.
 *
 * State is owned by the caller (the editor keeps the array + the mesh/entity
 * arrays); these functions are stateless.
 */

#ifndef __LIBMOD_3D_CHUNKTERRAIN_H
#define __LIBMOD_3D_CHUNKTERRAIN_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build `chunks*chunks` chunk meshes from a shared height array (side*side,
   row-major j*side+i) spanning world_size, and spawn one entity per chunk in
   `scene` using `material`. Requires (side-1) % chunks == 0. out_meshes and
   out_entities must hold chunks*chunks slots (either may be NULL). Returns the
   number of chunks built, or 0 on error. */
/* `holes` (side*side, 1 = skip that cell) punches holes in the terrain for cave
   entrances; pass NULL for no holes. */
int g3d_chunkterrain_build(const float *heights, int side, float world_size,
                           int chunks, float tiling, int scene, int material,
                           G3DMesh **out_meshes, int *out_entities,
                           const unsigned char *holes);

/* Rebuild (positions + seam-correct normals) the chunk meshes overlapping the
   world-space rectangle [minx,minz]..[maxx,maxz] from the (edited) heights, and
   re-upload only those. Call after a heightfield brush or a hole change. */
void g3d_chunkterrain_refresh(const float *heights, int side, float world_size,
                              int chunks, float tiling, G3DMesh **meshes,
                              float minx, float minz, float maxx, float maxz,
                              const unsigned char *holes);

/* ---- shared height array helpers (work on a raw float[side*side]) -------- */

/* Bilinear height at world (wx,wz). 0 outside bounds. */
float g3d_heightfield_height(const float *heights, int side, float world_size,
                             float wx, float wz);

/* Raise (strength>0) / lower (strength<0) within radius, smooth falloff. */
void g3d_heightfield_raise(float *heights, int side, float world_size,
                           float wx, float wz, float radius, float strength);

/* Smooth toward the local neighbourhood average (amount in [0,1]). */
void g3d_heightfield_smooth(float *heights, int side, float world_size,
                            float wx, float wz, float radius, float amount);

/* ---- .g3dh heightmap file (side, world_size, tiling, chunks, heights) ---- */

/* Write a heightmap. Returns 1 on success. */
int g3d_heightmap_write(const char *path, const float *heights, int side,
                        float world_size, float tiling, int chunks);

/* Read a heightmap into a freshly malloc'd array (caller frees). Fills the
   metadata out-params (any may be NULL). Returns the heights, or NULL. */
float *g3d_heightmap_read(const char *path, int *side, float *world_size,
                          float *tiling, int *chunks);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_CHUNKTERRAIN_H */
