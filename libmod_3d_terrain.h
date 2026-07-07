/*
 * libmod_3d_terrain.h - Runtime terrain editing (for the world editor)
 *
 * Operates on terrain meshes created by g3d_primitive_create_terrain(), which
 * carry grid metadata (terrain_side, terrain_world_size). All brush functions
 * recompute normals and re-upload the mesh to the GPU so edits are immediately
 * visible.
 */

#ifndef __LIBMOD_3D_TERRAIN_H
#define __LIBMOD_3D_TERRAIN_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bilinearly-sampled terrain height at world (x,z). Returns 0 if not a terrain
   or outside bounds. Useful for placing objects / a camera on the surface. */
float g3d_terrain_get_height(G3DMesh *mesh, float wx, float wz);

/* Raise (strength > 0) or lower (strength < 0) the terrain within `radius`
   world units of (wx,wz), with a smooth falloff toward the edge. */
int g3d_terrain_raise(G3DMesh *mesh, float wx, float wz, float radius,
                      float strength);

/* Smooth: blend heights toward their local neighbourhood average within radius.
   `amount` in [0,1] is the blend factor at the brush centre. */
int g3d_terrain_smooth(G3DMesh *mesh, float wx, float wz, float radius,
                       float amount);

/* Flatten: blend heights toward `target_h` within radius (amount in [0,1]). */
int g3d_terrain_flatten(G3DMesh *mesh, float wx, float wz, float radius,
                        float target_h, float amount);

/* Directly set one grid vertex height (grid coords 0..side-1). */
int g3d_terrain_set_vertex_height(G3DMesh *mesh, int ix, int iz, float h);

/* Recompute all normals from current heights and re-upload to the GPU. Call
   after a batch of g3d_terrain_set_vertex_height() edits. The brush functions
   above already call this themselves. */
void g3d_terrain_update(G3DMesh *mesh);

/* Heightmap persistence lives in libmod_3d_chunkterrain.h (.g3dh format, chunk
   aware). g3d_heightmap_read/write + g3d_chunkterrain_build. */

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_TERRAIN_H */
