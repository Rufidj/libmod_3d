/*
 * libmod_3d_pick.h - Screen-to-world picking (mouse -> terrain point)
 *
 * Unprojects a screen pixel through the camera and marches the ray against the
 * terrain heightfield, giving the world point under the cursor. This is how the
 * editor places things with the mouse.
 */

#ifndef __LIBMOD_3D_PICK_H
#define __LIBMOD_3D_PICK_H

#include "libmod_3d_camera.h"
#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cast a ray from the camera through screen pixel (sx,sy) in a (w x h) view and
   intersect the terrain heightfield (or the y=0 plane if terrain is NULL).
   Returns 1 on hit; read the point with g3d_pick_x/y/z(). */
int g3d_pick_terrain(G3DCamera *camera, float sx, float sy, float w, float h,
                     G3DMesh *terrain);

/* Like g3d_pick_terrain but against a raw shared height array (chunked terrain,
   which has no single mesh). side*side heights spanning world_size. */
int g3d_pick_heightfield(G3DCamera *camera, float sx, float sy, float w, float h,
                         const float *heights, int side, float world_size);

/* Pick a solid-collider entity under the cursor; returns entity id or -1. */
int g3d_pick_entity(G3DCamera *camera, float sx, float sy, float w, float h);

float g3d_pick_x(void);
float g3d_pick_y(void);
float g3d_pick_z(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PICK_H */
