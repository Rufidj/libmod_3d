/*
 * libmod_3d_collide.h - Simple collision queries against solid entity AABBs
 *
 * Entities flagged as colliders contribute their mesh world-space AABB as a
 * solid box. Provides character move-and-slide (a vertical cylinder vs boxes)
 * and a raycast (ray vs boxes), enough for walls/props blocking and picking.
 */

#ifndef __LIBMOD_3D_COLLIDE_H
#define __LIBMOD_3D_COLLIDE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Move a character (vertical cylinder, radius r, occupying [y, y+height]) from
   (x,z) by (dx,dz), sliding along solid entity AABBs. Result read back with
   g3d_collide_result_x()/z(). Returns 1. */
int g3d_collide_move_character(float x, float y, float z, float radius,
                               float height, float dx, float dz);
float g3d_collide_result_x(void);
float g3d_collide_result_z(void);

/* Highest solid-collider top under (x,z) (cylinder radius r) that is no higher
   than feet_y + a small step tolerance, i.e. a surface the character can stand
   on. Returns a very low value (-1e9) if none, so callers max() it with terrain. */
float g3d_collide_floor(float x, float z, float radius, float feet_y);

/* Cast a ray; returns the nearest hit distance along the (normalized) direction
   within maxdist, or -1 if nothing was hit. Hit point / entity read back with
   the accessors below. */
float g3d_collide_raycast(float ox, float oy, float oz,
                          float dx, float dy, float dz, float maxdist);
float g3d_collide_hit_x(void);
float g3d_collide_hit_y(void);
float g3d_collide_hit_z(void);
int g3d_collide_hit_entity(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_COLLIDE_H */
