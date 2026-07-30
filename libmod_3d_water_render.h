/*
 * libmod_3d_water_render.h - Drawing the unified water field
 *
 * Renders whatever libmod_3d_water_field.c says is wet, at the best quality the
 * GPU supports (see libmod_3d_glcaps.h). Sea, lakes and rivers are one draw
 * call over one persistent grid: the grid never changes, the field texture does.
 *
 * This replaces the old per-frame CPU mesh rebuild. Nothing here writes to the
 * field -- rendering is strictly a reader, so the simulation stays identical on
 * every platform and only the look degrades on weaker hardware.
 */

#ifndef __LIBMOD_3D_WATER_RENDER_H
#define __LIBMOD_3D_WATER_RENDER_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the water. Safe to call with no field: it does nothing. `flip_y` matches
   the projection Y-flip the renderer uses when drawing into a GRAPH FBO. */
void g3d_water_render(G3DCamera *camera, int flip_y);

/* Wave shape. `choppy` (0..1) trades round swell for sharp, peaked crests. */
void g3d_water_render_set_waves(float amplitude, float wavelength, float speed,
                                float choppy);

/* Optical properties. `absorption` is per-channel extinction per world unit --
   this is what makes water look like water, so keep red much higher than blue
   (clear water is roughly 0.45, 0.09, 0.045). `scatter` is the colour light
   picks up bouncing around inside the body. */
void g3d_water_render_set_optics(float ar, float ag, float ab,
                                 float sr, float sg, float sb,
                                 float roughness, float opacity);

/* Foam intensity (0 = none) and how strongly the surface bends what is behind
   it (0 = no refraction). */
void g3d_water_render_set_detail(float foam, float refraction);

/* Caustics: the net of light the surface casts on everything beneath it.
   0 disables the pass entirely; 1 is the default strength. */
void g3d_water_render_set_caustics(float strength);
float g3d_water_render_get_caustics(void);

/* Beach surf: waves that shoal, break into a white line and run up the sand.
   `amount` 0 disables it. `wavelength` is measured in units of DEPTH (the phase
   follows the bottom, which is what turns the crests to face the shore), `speed`
   is how fast they roll in, and `runup` is how far up the beach the swash
   reaches, in world units. */
void g3d_water_render_set_surf(float amount, float wavelength, float speed,
                               float runup);

/* Shape of the shore swell: crest `height` in world units, and the compass
   `direction_deg` it rolls in from. Direction only rules out at sea -- as the
   waves feel the bottom they refract and turn to face the shore anyway, which is
   what real ones do. */
void g3d_water_render_set_surf_wave(float height, float direction_deg);

/* Underwater look while the camera is submerged: how far you can see, and how
   strong the shafts of sunlight are (0 = none). */
void g3d_water_render_set_underwater(float visibility, float shafts);

/* Force the surface geometry path off/on regardless of hardware, for testing
   the fallback look. -1 = automatic (the default). */
void g3d_water_render_force_tessellation(int mode);

/* How far past the terrain the sea is drawn, as a multiple of the field size
   (default 4). Only matters when a sea level is set. */
void g3d_water_render_set_sea_extent(float multiple);

/* Read back the persistent foam field (one float per cell, 0..1) into `out`,
   which must hold side*side floats; `side` receives the field side. Returns 0
   when there is no foam field yet or the hardware has no compute. Exists so the
   foam can be measured, not just looked at. */
int g3d_water_render_foam_readback(float *out, int *side);

void g3d_water_render_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_RENDER_H */
