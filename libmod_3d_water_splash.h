/*
 * libmod_3d_water_splash.h - Water throwing droplets where it hits something
 *
 * The surface shader can bend, whiten and foam, but it cannot leave the plane.
 * Everything that makes moving water read as WATER once it meets an obstacle --
 * the burst off a rock in a rapid, the permanent cloud at the foot of a fall --
 * is loose droplets in the air, and those have to be particles.
 *
 * Nothing here is placed by hand. The splashes come from the same unified field
 * as the water itself: put a rock in a river and it splashes because the field
 * says fast water is arriving at it, and it stops when the river dries up.
 */

#ifndef __LIBMOD_3D_WATER_SPLASH_H
#define __LIBMOD_3D_WATER_SPLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Advance the splashes by `dt` seconds: spray at the foot of every waterfall,
   and bursts wherever moving water runs into a solid collider. Safe to call
   with no field, no falls and no rocks -- it does nothing. */
void g3d_water_splash_tick(float dt);

/* Overall intensity (0 disables it, 1 is the default). */
void g3d_water_splash_set_amount(float amount);
float g3d_water_splash_get_amount(void);

/* How fast water must be moving at an obstacle before it splashes, in world
   units per second (default 0.8). Raise it if a still lake is fizzing around
   every rock in it. */
void g3d_water_splash_set_threshold(float speed);

/* Splashes emitted since the last call, for tests and tooling. */
int g3d_water_splash_debug_count(void);

void g3d_water_splash_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_SPLASH_H */
