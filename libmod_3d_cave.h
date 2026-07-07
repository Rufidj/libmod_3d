/*
 * libmod_3d_cave.h - Voxel caves (hybrid with the heightfield terrain).
 *
 * The surface world stays a heightfield; caves/overhangs are separate VOXEL
 * volumes placed underground. Each cave is a 3D density field (solid rock vs air)
 * that the user carves with a 3D sphere brush; its surface is extracted with
 * marching cubes into a normal mesh (lit, shadowed, collidable). This gives real
 * 3D caves/arches where you want them without touching the water/collision/paint
 * systems that rely on the heightfield.
 */

#ifndef __LIBMOD_3D_CAVE_H
#define __LIBMOD_3D_CAVE_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a cave volume centred at world (cx,cy,cz), a cube `size` units per side,
   `res` voxels per side (res^3 total; 24..64 is sensible). Starts fully SOLID
   rock. Returns a cave id (>=0) or -1. */
int  g3d_cave_create(float cx, float cy, float cz, float size, int res);

/* Remove all caves (frees meshes + volumes). */
void g3d_cave_clear(void);
int  g3d_cave_count(void);

/* Set every voxel to `value` (>0 solid, <0 air). Use e.g. -1 to start empty. */
void g3d_cave_fill(int id, float value);

/* Initialize the volume as solid rock that matches a heightfield: density =
   terrainHeight(x,z) - voxelY, so the meshed surface equals the terrain and you
   carve caves INTO the ground. Pair with hiding the heightmap under the volume. */
void g3d_cave_fill_from_heightfield(int id, const float *heights, int side, float world_size);

/* March a ray from (ox,oy,oz) along (dx,dy,dz) up to `maxdist`; on the first
   crossing into solid rock, writes the hit point and returns 1 (else 0). Used as
   the 3D sculpt cursor: carve at the rock surface the mouse points at. */
int  g3d_cave_raycast(int id, float ox, float oy, float oz, float dx, float dy, float dz,
                      float maxdist, float *hx, float *hy, float *hz);

/* Carve or fill a sphere at world (wx,wy,wz), radius `radius`.
   mode 0 = carve out rock (make air), mode 1 = add rock back. `strength` 0..1
   softens the edit. Returns 1 if this cave changed. */
int  g3d_cave_edit(int id, float wx, float wy, float wz, float radius, int mode, float strength);

/* Rebuild the marching-cubes surface after edits; returns the mesh (owned by the
   cave, valid until the next rebuild/clear) or NULL if the volume is all solid/air. */
G3DMesh *g3d_cave_build_mesh(int id);
G3DMesh *g3d_cave_mesh(int id);          /* last built mesh (may be NULL) */

/* World-space bounds of a cave volume (for picking / placement). */
void g3d_cave_bounds(int id, float *cx, float *cy, float *cz, float *size);

/* Persist / restore the carved density volume (the shape must be saved; it is not
   procedurally regenerated like water). Returns 1 on success / a cave id. */
int  g3d_cave_save(int id, const char *path);
int  g3d_cave_load(const char *path);    /* returns the new cave id, or -1 */

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_CAVE_H */
