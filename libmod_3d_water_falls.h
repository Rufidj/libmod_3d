/*
 * libmod_3d_water_falls.h - Falling water (waterfalls) from the unified field
 *
 * A height field cannot describe falling water: a cliff turns into a ramp one
 * cell wide, which reads as water sliding rather than falling. This finds the
 * places where the field really does drop -- a wet cell whose neighbour is far
 * below -- and hangs a vertical sheet there.
 *
 * The sheets come from the SAME field as everything else, so a waterfall is not
 * an object anyone places: it appears wherever a river meets a ledge and
 * disappears when the water stops flowing. Geometry is rebuilt only when the
 * field changes, not per frame.
 */

#ifndef __LIBMOD_3D_WATER_FALLS_H
#define __LIBMOD_3D_WATER_FALLS_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the falls. Call after the water surface pass. Does nothing when there is
   no field or nothing is falling. */
void g3d_water_falls_render(G3DCamera *camera, int flip_y);

/* Smallest drop that counts as a fall rather than a slope, in world units
   (default 1.5). Raise it if gentle rapids are sprouting curtains. */
void g3d_water_falls_set_threshold(float drop);

/* Foam and landing-mist intensity (both default 1). */
void g3d_water_falls_set_style(float foam, float mist);

/* Where the falls LAND. Fills `out` with up to `max` entries of
   { x, y, z, fall_height, width } and returns how many. This is what the spray
   at the foot of a waterfall is placed with -- same source as the curtains, so
   it cannot drift off them. */
int g3d_water_falls_feet(float *out, int max);

/* How many sheets are currently being drawn, for tests and tooling. */
int g3d_water_falls_count(void);

void g3d_water_falls_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_FALLS_H */
