/*
 * libmod_3d_voxterrain.h - Full voxel terrain (marching cubes).
 *
 * The whole terrain is ONE chunked 3D density field (dens > 0 solid, < 0 air)
 * meshed with marching cubes. Unlike a heightfield it supports caves, overhangs
 * and arches, and a single "dig/add" brush sculpts it seamlessly (no patches).
 * Initialized from an existing heightmap so nothing looks different at first.
 *
 * A derived top-surface height (g3d_voxterrain_surface) keeps the water sim and
 * collision working (they still ask "what's the ground height at x,z").
 */

#ifndef __LIBMOD_3D_VOXTERRAIN_H
#define __LIBMOD_3D_VOXTERRAIN_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the voxel field from a heightfield (side*side over world_size). `ny`
   voxels tall span [ymin, ymax] (cover the terrain range + cave depth). Solid
   below the surface, air above. Clears any previous field. */
void g3d_voxterrain_init_from_heightfield(const float *heights, int side, float world_size,
                                          int ny, float ymin, float ymax);
void g3d_voxterrain_shutdown(void);
int  g3d_voxterrain_active(void);

/* Mesh every chunk and spawn its entity in `scene` with `material`. Call once
   after init. */
void g3d_voxterrain_build(int scene, int material);

/* Sculpt a CAVE: add (mode 1) or remove (mode 0) rock in a world SPHERE; makes
   caves/overhangs/arches. `strength` 0..1. Marks touched chunks dirty. */
void g3d_voxterrain_edit(float wx, float wy, float wz, float radius, int mode, float strength);

/* RAISE / LOWER the surface like a heightfield brush: shift the whole column in
   the XZ disc (radius) up (+delta) or down (-delta) with smooth falloff. Natural
   mountains/valleys, no blobs. */
void g3d_voxterrain_raise(float wx, float wz, float radius, float delta);

void g3d_voxterrain_remesh_dirty(void);

/* Top solid-surface height at world (x,z) (for water/collision), or a very
   negative value if the column is all air. */
float g3d_voxterrain_surface(float x, float z);

/* Fill `out` (nx*nz = side*side, row-major) with the top surface height per cell,
   so the water sim can run on the voxel terrain. */
void  g3d_voxterrain_export_heights(float *out);
int   g3d_voxterrain_side(void);

/* 3D sculpt cursor: first air->solid crossing of a ray; returns 1 + hit point. */
int  g3d_voxterrain_raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                            float maxdist, float *hx, float *hy, float *hz);

/* ---- Collision (walkable caves) --------------------------------------- */

/* 1 if world (x,y,z) is inside solid rock (for wall/ceiling collision). */
int   g3d_voxterrain_solid(float x, float y, float z);

/* Floor height a character at (x,y,z) stands on: the solid surface directly below
   (the cave floor when inside a cave, the terrain otherwise). If (x,y,z) is inside
   rock, returns the surface just above (push out). Very negative if none. */
float g3d_voxterrain_floor(float x, float y, float z);

/* 1 if a character capsule (feet at y, `radius` wide, `height` tall) would overlap
   rock at (x,z): robust wall/ceiling collision for movement. */
int   g3d_voxterrain_blocked(float x, float y, float z, float radius, float height);

/* Map size (X/Z span) so a game can keep the character inside the world. */
float g3d_voxterrain_worldsize(void);

/* Slope-aware character move (radius/height capsule): slide along X then Z, step
   up hills up to `maxstep`, block walls/cliffs/low ceilings. Read the resolved
   position with the getters. */
void  g3d_voxterrain_walk(float px, float py, float pz, float radius, float height,
                          float maxstep, float dx, float dz);
float g3d_voxterrain_walkx(void);
float g3d_voxterrain_walky(void);
float g3d_voxterrain_walkz(void);

/* Persist / restore the whole density field. Returns 1 on success. */
int  g3d_voxterrain_save(const char *path);
int  g3d_voxterrain_load(const char *path, int scene, int material);

/* ---- 3D voxel water (fills caves, waterfalls, floods; on the voxel grid) --- */
int  g3d_voxwater_active(void);
void g3d_voxwater_clear(void);
int  g3d_voxwater_add_source(float x, float y, float z, float rate);   /* a spring */
void g3d_voxwater_clear_sources(void);
int  g3d_voxwater_source_count(void);
int  g3d_voxwater_get_source(int n, float *x, float *y, float *z, float *rate);
void g3d_voxwater_step(float dt);          /* advance the sim (per frame) */
void g3d_voxwater_settle(float seconds);   /* fast-forward to steady state */
void g3d_voxwater_reflow(void);            /* recompute basins after the terrain changes
                                              (dig a breach -> water flows out through it) */
void g3d_voxwater_render(G3DCamera *camera, int flip_y);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_VOXTERRAIN_H */
