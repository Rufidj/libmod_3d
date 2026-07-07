/*
 * libmod_3d_particles.h - GPU point-sprite particle system
 *
 * Loose, volumetric effects that don't depend on a plane: waterfall spray,
 * splashes, mist, fountains (and later fire/smoke). Each emitter owns a pool of
 * particles updated on the CPU and drawn as soft round point sprites.
 */

#ifndef __LIBMOD_3D_PARTICLES_H
#define __LIBMOD_3D_PARTICLES_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create an emitter. Particles spawn inside the box (x,y,z)+/-(ex,ey,ez), with
   base velocity (vx,vy,vz) + random `spread`, fall under `gravity`, live `life`
   seconds and draw at world `size`. `count` particles recycle continuously.
   Returns an emitter id, or -1. */
int g3d_particles_create(float x, float y, float z, float ex, float ey,
                         float ez, float vx, float vy, float vz, float spread,
                         float gravity, float size, float life, int count);

/* Particle colour for an emitter. */
void g3d_particles_set_color(int id, float r, float g, float b);

/* Particles falling below `y` respawn (so a spray vanishes at the water). */
void g3d_particles_set_floor(int id, float y);

/* One-shot droplet burst (splashes): spawn `count` short-lived droplets at
   world (x,y,z) flying up and outward at ~`speed`, falling under gravity, drawn
   at `size`, living `life` seconds in colour (r,g,b). They do NOT recycle: once
   they fall/expire they're gone. Reusable core for water splashes (river mouths,
   objects entering the water, a character's stroke/footfall). */
void g3d_particles_burst(float x, float y, float z, int count, float speed,
                         float size, float life, float r, float g, float b);

/* Remove all emitters. */
void g3d_particles_clear(void);

/* Update + draw all emitters (called by the renderer). */
void g3d_particles_update_render(G3DCamera *camera, int flip_y);

void g3d_particles_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PARTICLES_H */
