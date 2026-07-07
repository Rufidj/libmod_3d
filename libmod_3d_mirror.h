/*
 * libmod_3d_mirror.h - Flat planar mirrors (multiple, each with own reflection)
 *
 * Each mirror is a flat quad on an arbitrary plane with its own reflection
 * texture: the renderer renders the scene mirrored across that plane into the
 * mirror's texture, and the quad samples it by screen position. No texture
 * asset needed. Several mirrors (and water) can reflect at the same time.
 */

#ifndef __LIBMOD_3D_MIRROR_H
#define __LIBMOD_3D_MIRROR_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Add a mirror centred at (px,py,pz), facing normal (nx,ny,nz), sized
   width x height in its plane. Returns its index, or -1 if the pool is full. */
int g3d_mirror_create(float px, float py, float pz,
                      float nx, float ny, float nz,
                      float width, float height);

/* Tint / V-flip a mirror by index (e.g. tint 0.9 = glass). */
void g3d_mirror_set_tint(int index, float r, float g, float b);
void g3d_mirror_set_flip(int index, int flip);

/* Beyond this distance from the camera a mirror is not re-rendered (it keeps
   its last frame). Lets you place many mirrors cheaply. Default 80. */
void g3d_mirror_set_max_distance(float dist);

/* Remove all mirrors. */
void g3d_mirror_clear(void);

/* Driven by the renderer: render the reflection of each *visible, near, facing*
   mirror into its texture (distance + frustum + facing culled), and (later in
   the frame) draw the mirror quads. */
void g3d_mirror_render_reflections(G3DCamera *camera);
void g3d_mirror_render_surfaces(G3DCamera *camera, int flip_y);

void g3d_mirror_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_MIRROR_H */
