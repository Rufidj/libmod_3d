/*
 * libmod_3d_water.h - Animated water surface
 *
 * A horizontal water plane rendered with its own shader: vertex waves (sum of
 * sine waves) animated over time, analytic normals, Fresnel blend, sun specular
 * and transparency. Rendered after the opaque forward pass with alpha blending.
 */

#ifndef __LIBMOD_3D_WATER_H
#define __LIBMOD_3D_WATER_H

#include "libmod_3d_math.h"
#include "libmod_3d_camera.h"
#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create / configure the water surface at world height `level`, spanning
   size x size units, subdivided `subdiv` times per side. Enables water. */
int g3d_water_create(float level, float size, int subdiv);

/* Wave tuning: max amplitude, base wavelength, animation speed. */
void g3d_water_set_waves(float amplitude, float wavelength, float speed);

/* Deep and shallow/edge colours (Fresnel blends between them). */
void g3d_water_set_color(float dr, float dg, float db, float sr, float sg,
                         float sb);

/* Enable / disable water rendering. */
void g3d_water_set_enabled(int enabled);
int g3d_water_is_enabled(void);

/* Global ripple intensity multiplier (0 = no ripples). */
void g3d_water_set_ripple_strength(float strength);

/* Spawn an expanding ring ripple on the water surface at world (x,z). strength
   ~1 is a normal splash. Call repeatedly for a continuous source (river mouth,
   object in the water) or per event (a swimmer's stroke). */
void g3d_water_ripple(float x, float z, float strength);

/* Ripple + a short burst of splash droplets at world (x,y,z). */
void g3d_water_splash(float x, float y, float z, float strength);

/* Continuous ripple sources (river mouths, etc.): the engine re-emits a ripple
   at each on a timer. Register once (editor/scene loader); g3d_water_tick() (run
   by the renderer each frame) keeps them going. */
void g3d_water_clear_ripple_sources(void);
void g3d_water_add_ripple_source(float x, float z, float strength);
void g3d_water_tick(void);

/* Auto-detect if the camera is submerged and drive the underwater screen effect
   (called by the renderer each frame). */
void g3d_water_update_underwater(G3DCamera *camera);

/* Optional surface texture (GL handle, 0 = none): adds scrolling ripple detail
   and tint. Use a tileable water texture. */
void g3d_water_set_texture(unsigned int gl_handle);

/* Planar reflections: render the scene mirrored and blend it into the water by
   Fresnel. strength scales the effect. flip toggles the sample V if it comes
   out upside-down on a given platform. */
void g3d_water_set_reflection(int enable, float strength);
void g3d_water_set_ssr(int enable, float strength);   /* screen-space reflections on water */
void g3d_water_set_ocean(float dirx, float dirz, float swell_amp);  /* swell + breaking surf (0=off) */
void g3d_water_set_tessellation(int enable);   /* GL4 geometric wave volume (default on if GL4) */
void g3d_water_set_reflection_flip(int flip);
int g3d_water_reflection_enabled(void);
float g3d_water_get_level(void);

/* Render the water pass (called by the renderer after the forward pass).
   flip_y matches the projection Y-flip used when rendering into a GRAPH FBO. */
void g3d_water_render_pass(G3DCamera *camera, int flip_y);

/* ---- Fluid zones: multiple placed liquid patches (pools/lakes) ----------
   Unlike the single global water plane, fluids are localized rectangular
   surfaces you place where you want. They share one visual style (waves, colours,
   texture) but each has its own centre, size and depth. They reuse the water
   shader (waves + fresnel + texture); no planar reflection per zone. */

void g3d_fluid_clear(void);

/* Add a fluid zone centred at world (cx,cz), size_x by size_z units, with its
   surface at height `level` and `depth` units of water below (depth deepens the
   colour). Returns a zone id (>=0) or -1. */
int g3d_fluid_add(float cx, float cz, float size_x, float size_z,
                  float level, float depth);

/* Shared style for all fluid zones. tex_handle 0 = no texture. opacity 0..1. */
void g3d_fluid_set_style(float amp, float len, float speed,
                         float dr, float dg, float db,
                         float sr, float sg, float sb,
                         unsigned int tex_handle, float opacity);

/* Add a fluid zone from a caller-built world-space mesh (e.g. a flood-filled
   lake). Engine takes ownership of the mesh. depth deepens the colour. */
int g3d_fluid_add_mesh(G3DMesh *mesh, float depth);

/* Material kind for all fluid zones: 0 = water, 1 = lava (emissive molten glow,
   slow flowing crust, opaque, no reflection/refraction). */
void g3d_fluid_set_kind(int kind);
int g3d_fluid_get_kind(void);

void g3d_fluid_render_pass(G3DCamera *camera, int flip_y);
int g3d_fluid_count(void);

/* Draw one world-space water mesh with the shared water shader + current style
   (used by the live height-field water sim, which rebuilds its mesh per frame).
   The mesh packs flow direction in normal.xz and shore depth in texcoord.x. */
void g3d_water_draw_mesh(G3DMesh *mesh, G3DCamera *camera, int flip_y);

/* ---- Lake fill (flood the terrain from a seed up to its spill rim) -------
   Works on a shared height array (side*side spanning world_size, row-major). */

/* Auto water level: the elevation at which water poured at (seedX,seedZ) would
   first overflow the basin (minimax height along the cheapest path to the map
   edge). Click in a hole -> get the brim level. */
float g3d_fluid_spill_level(const float *heights, int side, float world_size,
                            float seedX, float seedZ, const unsigned char *blocked);

/* Build a lake surface mesh. The FOOTPRINT (shape) is the cells connected to
   (seedX,seedZ) whose terrain is below `footprintLevel` (use the spill level so
   it matches the hole); the water surface is placed at `surfaceY`. Decoupling
   the two means raising/lowering the surface keeps the hole's shape instead of
   flooding the whole map. Returns the mesh (pass to g3d_fluid_add_mesh) or NULL.
   Writes the deepest depth (surfaceY - min terrain) to *out_depth if non-NULL. */
G3DMesh *g3d_fluid_build_lake(const float *heights, int side, float world_size,
                              float seedX, float seedZ, float footprintLevel,
                              float surfaceY, const unsigned char *blocked,
                              unsigned char *out_filled, float *out_depth,
                              float max_radius);   /* >0 = cap fill to a disc (no map-wide flood) */

/* Mark the cells within `width` of a river polyline into mask[side*side]=1, so a
   lake's flood-fill treats the river as a wall (separate water bodies). */
void g3d_river_mark_mask(const float *pts, int n, int side, float world_size,
                         float width, unsigned char *mask);

/* How many leading river points to keep before it first enters a lake (cells in
   `mask`). Trim the river there so it stops at the lake instead of running under
   it. Returns n if it never touches a lake. */
int g3d_river_trim_count(const float *pts, int n, int side, float world_size,
                         const unsigned char *mask);

/* Build a RIVER surface: flood-fill the terrain cells within `width` of the
   polyline `pts` (n xyz points; their Y is the water-surface level along the
   course, so it can slope and drop) that lie below that level. The mesh carries
   the flow direction in each vertex normal.xz, so the water shader scrolls the
   ripples downstream. Returns the mesh (pass to g3d_fluid_add_mesh) or NULL. */
G3DMesh *g3d_fluid_build_river(const float *pts, int n, const float *heights,
                               int side, float world_size, float width,
                               float *out_depth);

/* Free GPU resources. */
void g3d_water_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WATER_H */
