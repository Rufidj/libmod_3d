/*
 * libmod_3d_flow.h - Flowing water surfaces (waterfalls / rivers)
 *
 * A flow surface is a ribbon quad whose texture scrolls along its length over
 * time, giving the impression of moving water. Vertical ribbons make waterfalls,
 * sloped ones make rivers. Rendered transparent after the opaque pass.
 */

#ifndef __LIBMOD_3D_FLOW_H
#define __LIBMOD_3D_FLOW_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Add a flow ribbon from a top edge centre (tx,ty,tz) to a bottom edge centre
   (bx,by,bz), `width` wide (along X), texture tiled `tiling` along its length,
   scrolling at `speed`. Returns a flow id, or -1. */
int g3d_flow_add(float tx, float ty, float tz, float bx, float by, float bz,
                 float width, float speed, float tiling);

/* Add a river/cascade that conforms to the terrain: a ribbon along the XZ line
   (x0,z0)->(x1,z1), `width` wide, whose vertices follow the terrain height
   (+ y_offset). Reacts to the terrain relief. */
int g3d_flow_add_river(void *terrain_mesh, float x0, float z0, float x1,
                       float z1, float width, float y_offset, float speed,
                       float tiling);

/* Add a river that follows a polyline of `n` points (pts = n xyz triples, already
   at terrain height). The ribbon is `width` wide, lifted by y_offset, its texture
   scrolling at `speed` and tiled `tiling` times along the whole length. The water
   flows in the point order. Returns a flow id, or -1. */
int g3d_flow_add_path(const float *pts, int n, float width, float y_offset,
                      float speed, float tiling);

/* Scan a river polyline (n xyz points) over the heightfield and add a falling
   water sheet wherever the terrain drops steeply -> automatic waterfalls. */
void g3d_river_add_waterfalls(const float *pts, int n, const float *heights,
                              int side, float world_size, float width);

/* Surface texture (GL handle) shared by all flows. */
void g3d_flow_set_texture(unsigned int gl_handle);

/* Water colour shared by all flows. */
void g3d_flow_set_color(float r, float g, float b);

/* Clip flow fragments below world height `y` (e.g. the water level, so a
   waterfall vanishes at the surface instead of showing underwater). */
void g3d_flow_set_clip(float y);

/* Remove all flow surfaces. */
void g3d_flow_clear(void);

/* Render all flow surfaces (called by the renderer after the water pass). */
void g3d_flow_render_pass(G3DCamera *camera, int flip_y);

void g3d_flow_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_FLOW_H */
