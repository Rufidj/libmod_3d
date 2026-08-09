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

/* Solid things standing IN the falling water, so the curtain parts around them
   and closes again below instead of passing through the rock. `boxes` is `n`
   entries of { min_x, min_y, min_z, max_x, max_y, max_z } and the call REPLACES
   the whole set; n=0 clears it.

   These are NOT the same boxes as the field's obstacles. Those divert the flow
   and only cover what is standing in water deep enough to register, which a rock
   halfway down a cliff never is; and they are a flat mask, with no height, which
   is exactly what a vertical curtain needs to know. Keeping the two separate
   leaves the flow behaviour untouched.

   The geometry is only rebuilt when the set really changes, so a rock sitting
   still costs nothing. */
void g3d_water_falls_set_obstacles(const float *boxes, int n);

/* How many sheets are currently being drawn, for tests and tooling. */
int g3d_water_falls_count(void);

/* How many curtains got parted by a rock last build. For tests: a fall that
   should be split and is not reports 0 here. */
int g3d_water_falls_split_count(void);

void g3d_water_falls_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_FALLS_H */
