/*
 * libmod_3d_watersim.h - Unified height-field water (live shallow-water sim)
 *
 * One water system instead of separate lakes and rivers. Water is a per-cell
 * DEPTH field over the terrain heightfield; the surface level of a cell is
 * terrain + depth. Water flows downhill under gravity (virtual-pipes / shallow
 * water model, Mei et al.), so lakes (water pooling in basins), rivers (water
 * running down slopes), waterfalls, deltas and lake<->river junctions are all
 * emergent from ONE continuous surface -- no meshes to stitch.
 *
 * Authoring = sources + gravity: place springs / rain / a sea level and the
 * water finds its own level and channels. The simulation runs live each frame.
 */

#ifndef __LIBMOD_3D_WATERSIM_H
#define __LIBMOD_3D_WATERSIM_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the sim over a terrain heightfield: `heights` is side*side row-major,
   spanning world_size units centred on the origin. A private copy is kept, so
   call g3d_watersim_set_terrain() after sculpting to update it. Clears any
   previous state. */
void g3d_watersim_init(const float *heights, int side, float world_size);

/* Replace the terrain heights (after the user sculpts). Same dimensions. */
void g3d_watersim_set_terrain(const float *heights);

/* Free all sim + GPU resources. */
void g3d_watersim_shutdown(void);

int g3d_watersim_active(void);

/* ---- Sources / boundary conditions ------------------------------------- */

/* A spring at world (x,z): adds `rate` units of water depth per second into that
   cell for as long as it exists. Returns a source id (>=0) or -1. rate<0 is a
   drain (sink). */
int  g3d_watersim_add_source(float x, float z, float rate);
void g3d_watersim_clear_sources(void);
int  g3d_watersim_source_count(void);

/* Read back a source (i in [0,count)) and the global params, for saving the
   scene. get_source returns 1 on success. sea is very negative if disabled. */
int  g3d_watersim_get_source(int i, float *x, float *z, float *rate);
void g3d_watersim_get_params(float *rain, float *sea, float *evap, float *flow);

/* Uniform rainfall over the whole map (depth per second, 0 = off). */
void g3d_watersim_set_rain(float rate);

/* Ocean: every cell whose terrain is below `y` is kept filled up to `y`.
   Pass -1e30 to disable. */
void g3d_watersim_set_sea_level(float y);

/* Evaporation / soak (depth per second removed everywhere) keeps rivers thin and
   dries stray puddles. 0 = water only leaves by flowing off the map edges. */
void g3d_watersim_set_evaporation(float rate);

/* Overall flow speed multiplier (viscosity); 1 = default. Lava would use a low
   value (slow, thick). */
void g3d_watersim_set_flow_scale(float s);

/* ---- Simulation -------------------------------------------------------- */

/* Advance the water by dt seconds (call once per frame). Internally sub-steps
   for numerical stability, so any dt is safe. */
void g3d_watersim_step(float dt);

/* Fast-forward `seconds` of simulation in one call, so the water reaches its
   steady state immediately (a spring's lake+river appears already full instead
   of slowly filling). Call after placing a source or at scene load. */
void g3d_watersim_settle(float seconds);

/* Water depth field (side*side), for queries (e.g. is a point underwater). */
const float *g3d_watersim_depth(void);
int          g3d_watersim_side(void);

/* Water surface level at world (x,z): terrain+depth, or a very negative value
   where there is no water. */
float g3d_watersim_level_at(float x, float z);

/* ---- Rendering --------------------------------------------------------- */

/* Rebuild the water surface mesh from the current field and draw it with the
   shared water shader (waves, fresnel, refraction, flow scroll, foam). Called by
   the renderer after the opaque pass. flip_y matches the GRAPH FBO projection. */
void g3d_watersim_render(G3DCamera *camera, int flip_y);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATERSIM_H */
