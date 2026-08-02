/*
 * libmod_3d_water_field.h - The unified water field
 *
 * ONE description of where water is in the world, replacing the separate lake,
 * river, sea and waterfall subsystems. Water is a per-cell DEPTH over the
 * terrain heightfield; a cell's surface level is terrain + depth. Water moves
 * downhill under gravity (shallow-water / virtual-pipes, Mei et al. 2007), so
 * lakes, rivers, deltas, waterfalls and lake<->river junctions all EMERGE from
 * one continuous field. Authoring is springs + rain + a sea level; the water
 * finds its own level and carves its own channels.
 *
 * This module is pure CPU and has no GL dependency on purpose: it is the
 * simulation and the physics query, and it must behave IDENTICALLY on every
 * platform. A boat floats the same on a phone as on a desktop GPU; only the
 * rendering of the surface changes with the quality tier. Rendering reads this
 * field (see libmod_3d_water.c) and never writes it.
 *
 * Coordinates: the field spans `world_size` units centred on the origin, as a
 * `side` x `side` row-major grid of samples, matching the terrain heightfield.
 */

#ifndef __LIBMOD_3D_WATER_FIELD_H
#define __LIBMOD_3D_WATER_FIELD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returned by the level queries where there is no water at all. Compare with
   G3D_NO_WATER_TEST rather than for equality. */
#define G3D_NO_WATER      (-1.0e30f)
#define G3D_NO_WATER_TEST (-1.0e29f)   /* level < this  =>  dry */

/* ---- Lifetime ---------------------------------------------------------- */

/* Start a field over a terrain heightfield (side*side row-major, spanning
   world_size units centred on the origin). A private copy of the terrain is
   kept. Clears any previous state. Returns 1 on success. */
int  g3d_waterfield_init(const float *terrain, int side, float world_size);

/* Replace the terrain after sculpting (same dimensions). Water depths are
   preserved, so an edit displaces the water rather than resetting it. */
void g3d_waterfield_set_terrain(const float *terrain);

void g3d_waterfield_shutdown(void);
int  g3d_waterfield_active(void);

/* Grid description, for code that wants to walk the field itself. */
int   g3d_waterfield_side(void);
float g3d_waterfield_world_size(void);
const float *g3d_waterfield_depth_array(void);    /* side*side, metres of water */
const float *g3d_waterfield_terrain_array(void);  /* side*side, terrain Y       */

/* Per-cell flow velocity components (side*side each), for bulk consumers such
   as the GPU upload. Use g3d_waterfield_flow_at() for single interpolated
   lookups; these are the raw grid values. */
void g3d_waterfield_flow_arrays(const float **vx, const float **vz);

/* ---- Sources and boundary conditions ----------------------------------- */

/* A spring at world (x,z) adding `rate` metres of depth per second. A negative
   rate is a drain. Returns a source id (>=0) or -1 if full. */
int  g3d_waterfield_add_spring(float x, float z, float rate);
void g3d_waterfield_clear_springs(void);

/* Same, but owned by a caller-chosen tag so it can retire only its own.
   Anything that re-creates its water wholesale on every edit -- an editor
   rebuilding a scene -- MUST be able to remove exactly the springs it added, or
   they pile up on each rebuild and the map floods without limit. Tag 0 belongs
   to plain add_spring. */
#define G3D_WF_TAG_WATERFALL 1   /* springs created by a placed waterfall */
#define G3D_WF_TAG_RIVER     2   /* the head spring feeding a placed river */
int  g3d_waterfield_add_spring_tagged(float x, float z, float rate, int tag);
void g3d_waterfield_clear_springs_tagged(int tag);
int  g3d_waterfield_spring_count(void);
int  g3d_waterfield_get_spring(int i, float *x, float *z, float *rate);

/* Every cell whose terrain lies below `y` is held filled to `y`: the sea.
   Pass G3D_NO_WATER to disable. */
void g3d_waterfield_set_sea_level(float y);
float g3d_waterfield_get_sea_level(void);

/* Uniform rainfall (metres of depth per second, 0 = off). */
void g3d_waterfield_set_rain(float rate);

/* Evaporation / soak, removed everywhere (metres per second). This is what
   bounds a spring: the wider its water spreads the more it loses, so a constant
   spring settles into a finite lake and river instead of flooding the map.
   0 = water only leaves across the map edges. */
void g3d_waterfield_set_evaporation(float rate);

/* Flow speed multiplier (1 = water). Low values read as thick and slow, for
   lava or mud. */
void g3d_waterfield_set_viscosity(float scale);

/* Tune the surface the flow is routed over (defaults: 1 pass, 0.25 tolerance).
   Terrain noise makes micro-pits that trap water and stop rivers flowing, so
   the flux step routes over a smoothed copy of the terrain. `tolerance` bounds
   how far that copy may deviate from the real ground, in world units: set it to
   the noise scale you want ironed out. It is not just a safety rail -- an
   unbounded smooth raises the floor of real basins, which dents the surface of
   what should be a flat lake, and shaves ridges, which leaks water over
   barriers that should hold it. Levels and depths always use the REAL terrain.
   Pass 0 passes for exact routing. */
void g3d_waterfield_set_routing(int passes, float tolerance);

void g3d_waterfield_get_params(float *rain, float *sea, float *evap, float *visc);

/* ---- Authoring: placing water directly ---------------------------------- */

/* These exist so hand-placed water (an editor's lake and river tools) becomes
   the SAME field as everything else, instead of a separate mesh with its own
   shader. Once placed, the water is ordinary water: it flows, joins the sea,
   feeds a waterfall over a ledge, and floats objects. The simulation will then
   move it, so a lake in a leaky basin drains -- which is the point. */

/* Placed water is EXEMPT FROM EVAPORATION, and nothing more. A hand-placed lake
   would otherwise be a one-off pour that quietly dries up within a couple of
   minutes, taking the author's work with it.
 *
   It is deliberately NOT topped back up to a fixed level. That was tried and it
   is far worse: a lake refilled every step becomes an inexhaustible spring, so
   it spills over its rim, the top-up replaces exactly what left, and the water
   downstream grows without bound until the whole map floods. Exemption keeps the
   body of water finite -- it can still spill, drain through a leaky basin, and
   reach equilibrium -- while making sure it never simply evaporates away.
   Clearing is the caller's job when it rebuilds its water. */
void g3d_waterfield_clear_holds(void);

/* Flood the basin containing (x,z) up to `level`: every connected cell whose
   terrain lies below it is filled to that surface height. `max_radius` (<=0 for
   unlimited) caps how far the fill may spread, so clicking a shallow dip cannot
   flood the whole map. Returns the number of cells filled. */
int g3d_waterfield_fill_basin(float x, float z, float level, float max_radius);

/* Lay a channel of water along a polyline (`pts` = n xyz triples whose Y is the
   intended water surface at that point, so the course can descend). Cells within
   `width`/2 of the line are raised to that surface. Returns cells filled. */
int g3d_waterfield_add_channel(const float *pts_xyz, int n, float width);

/* ---- Simulation --------------------------------------------------------- */

/* Advance by dt seconds; sub-steps internally for stability, so any dt is safe. */
void g3d_waterfield_step(float dt);

/* Fast-forward `seconds` in one call so the water is already at its steady
   state (a spring's lake and river appear full instead of slowly filling).
   Call after placing sources or at scene load. */
void g3d_waterfield_settle(float seconds);

/* Bump the internal revision so renderers know the field changed. Read it to
   skip GPU re-uploads when nothing moved. */
unsigned int g3d_waterfield_revision(void);

/* ---- Queries (physics, gameplay, buoyancy) ------------------------------ */

/* Water surface level at world (x,z), bilinearly interpolated over the WET
   neighbourhood, or G3D_NO_WATER where there is none. This is the buoyancy
   query; it deliberately ignores wave displacement, which is a rendering
   detail added on top (see g3d_water_wave_height_at). */
float g3d_waterfield_level_at(float x, float z);

/* Depth of water at (x,z); 0 where dry. */
float g3d_waterfield_depth_at(float x, float z);

/* Solid things standing in the water: water flows AROUND them instead of
   through them. `boxes` is `n` entries of { min_x, min_z, max_x, max_z }, and
   the call REPLACES the whole set -- pass n=0 to clear it. Returns how many
   cells are blocked.

   Replacing rather than adding is deliberate: the caller rescans the scene
   periodically, so an additive API would stack duplicates forever. The field
   revision only moves when the mask really changed, so a rock sitting still
   costs nothing. */
int g3d_waterfield_set_obstacles(const float *boxes, int n);

/* Is (x,z) inside a registered obstacle? */
int g3d_waterfield_obstacle_at(float x, float z);

/* Horizontal flow velocity at (x,z) in units/second. Drives buoyant objects
   downstream, particle spawn direction and the shader's flow-map scroll. */
void  g3d_waterfield_flow_at(float x, float z, float *vx, float *vz);

/* Terrain height at (x,z) (bilinear), for the shore/depth computations. */
float g3d_waterfield_terrain_at(float x, float z);

/* Bounding box of the wet region in grid cells, inclusive; returns 0 if the
   field is completely dry. Lets renderers and the sim skip empty space. */
int g3d_waterfield_wet_bounds(int *i0, int *j0, int *i1, int *j1);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_FIELD_H */
