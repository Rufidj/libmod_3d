/*
 * libmod_3d_watersim.c - Compatibility shim over the unified water system
 *
 * This used to be a self-contained shallow-water sim that rebuilt its surface
 * mesh on the CPU every frame. The simulation now lives in
 * libmod_3d_water_field.c and the drawing in libmod_3d_water_render.c, so what
 * remains here is the old entry points forwarding to them. Callers (the editor,
 * saved scenes, .prg scripts) keep working unchanged and silently get the new
 * renderer.
 *
 * The name mapping is almost one to one; the only real translation is
 * "source" -> "spring" and "flow scale" -> "viscosity".
 */

#include "libmod_3d_watersim.h"
#include "libmod_3d_water.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"

void g3d_watersim_init(const float *heights, int side, float world_size) {
    g3d_waterfield_init(heights, side, world_size);
}

void g3d_watersim_set_terrain(const float *heights) {
    g3d_waterfield_set_terrain(heights);
}

void g3d_watersim_shutdown(void) {
    g3d_water_render_shutdown();
    g3d_waterfield_shutdown();
}

int g3d_watersim_active(void) { return g3d_waterfield_active(); }

int g3d_watersim_add_source(float x, float z, float rate) {
    /* Un manantial es motivo suficiente para que exista el campo. Antes hacia
       falta que ANTES hubiera un mar o un lago que lo creara: en el editor eso
       pasaba siempre, y en un juego con solo manantiales la llamada fallaba en
       silencio y el rio no aparecia nunca. */
    if (!g3d_waterfield_active() && !g3d_water_ensure_field()) return -1;
    return g3d_waterfield_add_spring(x, z, rate);
}

void g3d_watersim_clear_sources(void) { g3d_waterfield_clear_springs(); }
int  g3d_watersim_source_count(void)  { return g3d_waterfield_spring_count(); }

int g3d_watersim_get_source(int i, float *x, float *z, float *rate) {
    return g3d_waterfield_get_spring(i, x, z, rate);
}

void g3d_watersim_get_params(float *rain, float *sea, float *evap, float *flow) {
    g3d_waterfield_get_params(rain, sea, evap, flow);
}

void g3d_watersim_set_rain(float rate)        { g3d_waterfield_set_rain(rate); }
void g3d_watersim_set_sea_level(float y)      { g3d_waterfield_set_sea_level(y); }
void g3d_watersim_set_evaporation(float rate) { g3d_waterfield_set_evaporation(rate); }
void g3d_watersim_set_flow_scale(float s)     { g3d_waterfield_set_viscosity(s); }

void g3d_watersim_step(float dt)        { g3d_waterfield_step(dt); }
void g3d_watersim_settle(float seconds) { g3d_waterfield_settle(seconds); }

const float *g3d_watersim_depth(void) { return g3d_waterfield_depth_array(); }
int          g3d_watersim_side(void)  { return g3d_waterfield_side(); }

float g3d_watersim_level_at(float x, float z) {
    return g3d_waterfield_level_at(x, z);
}

void g3d_watersim_render(G3DCamera *camera, int flip_y) {
    g3d_water_render(camera, flip_y);
}
