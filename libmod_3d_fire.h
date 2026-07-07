/*
 * libmod_3d_fire.h - Procedural shader fire (torches, campfires).
 *
 * Each fire is a camera-facing billboard drawn with an animated noise flame shader
 * (HDR-bright so it blooms) plus a warm POINT LIGHT that flickers, lighting the scene.
 * Great for night scenes lit by torches.
 */
#ifndef __LIBMOD_3D_FIRE_H
#define __LIBMOD_3D_FIRE_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Add a fire at world (x,y,z) (the base of the flame), `size` tall-ish. Spawns a warm
   flickering point light in the active scene. Returns the fire id, or -1. */
int  g3d_fire_add(float x, float y, float z, float size);
void g3d_fire_move(int id, float x, float y, float z);
void g3d_fire_clear(void);
int  g3d_fire_count(void);

/* Advance the flicker + draw all fire billboards (called by the renderer each frame). */
void g3d_fire_render(G3DCamera *camera, int flip_y);

#ifdef __cplusplus
}
#endif

#endif
