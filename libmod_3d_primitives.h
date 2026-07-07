/*
 * libmod_3d_primitives.h - Primitive Mesh Generation
 *
 * Helper functions to create basic shapes for testing/debugging
 */

#ifndef __LIBMOD_3D_PRIMITIVES_H
#define __LIBMOD_3D_PRIMITIVES_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a unit cube (1x1x1) centered at origin */
G3DMesh *g3d_primitive_create_cube(void);

/* Create a unit sphere (radius 1) */
G3DMesh *g3d_primitive_create_sphere(int segments);

/* Create a plane (1x1) on XZ axis */
G3DMesh *g3d_primitive_create_plane(void);

/* Create a procedural noise terrain (heightmap on XZ, centered at origin).
   Texcoords are scaled by `tiling` for use with a repeating texture. */
G3DMesh *g3d_primitive_create_terrain(int grid, float world_size, float height,
                                      float tiling, unsigned int seed);

/* Build a terrain mesh from an explicit (side x side) height array (row-major,
   index j*side+i). Computes normals + tiled texcoords and sets terrain metadata.
   Used to rebuild sculpted/saved terrains. */
G3DMesh *g3d_primitive_terrain_from_heights(int side, float world_size,
                                            const float *heights, float tiling);

/* Create a cliff/coast terrain: noise pushed through a tanh S-curve so basins
   flatten (water pools), slopes steepen into cliffs and highlands form plateaus.
   steepness ~1 gentle .. ~6 dramatic; water_floor (0..1) sinks basins below 0. */
G3DMesh *g3d_primitive_create_cliffs(int grid, float world_size, float height,
                                     float tiling, unsigned int seed,
                                     float steepness, float water_floor);

/* Create a radial mountain with fbm detail and a carved front gorge (for a
   waterfall/river). Base sunk below 0 so water forms a lake around it. */
G3DMesh *g3d_primitive_create_mountain(int grid, float world_size, float peak,
                                       float tiling, unsigned int seed,
                                       float channel);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PRIMITIVES_H */
