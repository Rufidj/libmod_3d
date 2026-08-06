/*
 * libmod_3d_erosion.h - Hydraulic erosion over a height field
 *
 * A terrain out of noise has round, interchangeable hills: every slope looks
 * like every other slope. What makes real ground look like a PLACE is that
 * water has been running over it for a long time -- it cuts gullies, they join
 * into valleys, the ridges between them sharpen, and everything the water tore
 * off piles up as fans where the slope eases.
 *
 * This is the same virtual-pipes model the water simulation already uses (see
 * libmod_3d_water_field.h), with two things added: moving water picks up
 * ground, and slowing water drops it again. That is the whole of erosion.
 *
 * Pure CPU and self-contained: it takes a height array and gives it back
 * eroded. It does NOT touch the live water field -- eroding a terrain while a
 * river is running on it would fight the simulation for the same cells.
 */

#ifndef __LIBMOD_3D_EROSION_H
#define __LIBMOD_3D_EROSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float rain;        /* water added per cell per step (0.01 = suave)        */
    float evaporation; /* fraction of water lost per step (0.02 = suave)      */
    float capacity;    /* how much a fast, deep flow can carry (higher = mas) */
    float dissolve;    /* how fast under-loaded water tears ground up         */
    float deposit;     /* how fast over-loaded water drops it again           */
    float min_slope;   /* floor on the slope used for capacity: without it,   */
                       /* flat ground carries nothing and channels never start*/
    float talus;       /* thermal erosion: slope (rise per cell) above which  */
                       /* loose material slides. 0 = off                      */
    float gravity;
} G3DErosionParams;

/* Sensible starting point (also what the editor opens with). */
void g3d_erosion_defaults(G3DErosionParams *p);

/* Erode `heights` (side*side, row-major, i -> x and j -> z) in place over
   `iterations` steps. `world_size` is the terrain's extent in world units and
   sets the cell spacing, so the same parameters behave the same on a 400-unit
   map and on a 4000-unit one. Returns the number of steps actually run.

   Cost is O(iterations * side^2); 50 steps on 161x161 is instant, 500 is a
   second or so. */
int g3d_erosion_run(float *heights, int side, float world_size,
                    int iterations, const G3DErosionParams *params);

/* Thermal erosion on its own: material above the talus angle slides downhill
   until the slope is stable. Rounds off the impossible spikes a height brush
   leaves behind, without needing water. */
int g3d_erosion_thermal(float *heights, int side, float world_size,
                        int iterations, float talus);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_EROSION_H */
