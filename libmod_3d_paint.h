/*
 * libmod_3d_paint.h - Terrain texture painting (for the world editor)
 *
 * A paint canvas is an RGBA image stretched 0..1 over a terrain. You fill it
 * with a base texture and then paint other textures onto it with brushes; the
 * result is a single baked texture the map can use as its albedo. Use a terrain
 * created with tiling = 1 so the canvas maps 1:1 over the surface.
 */

#ifndef __LIBMOD_3D_PAINT_H
#define __LIBMOD_3D_PAINT_H

#include "libmod_3d_mesh.h"
#include "libmod_3d_texture.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    unsigned char *pixels;   /* RGBA, width*height*4 */
    unsigned int gl_handle;  /* OpenGL texture */
    int gpu_uploaded;
    void *cached_tex;        /* lazy G3DTexture wrapper (use as material albedo) */
} G3DPaintCanvas;

/* Create an empty (opaque black) canvas and its GL texture. */
G3DPaintCanvas *g3d_paint_create(int width, int height);

/* Free the canvas (CPU + GPU). */
void g3d_paint_free(G3DPaintCanvas *c);

/* Fill the whole canvas by tiling `src` `tiling` times across it (base layer). */
void g3d_paint_fill(G3DPaintCanvas *c, G3DTexture *src, float tiling);

/* Paint `src` onto the canvas within `radius` world units of (wx,wz) on the
   given terrain, tiled `src_tiling` times across the terrain, with a smooth
   brush falloff and `opacity` in [0,1]. Re-uploads the affected texture. */
int g3d_terrain_paint(G3DMesh *terrain, G3DPaintCanvas *c, G3DTexture *src,
                      float src_tiling, float wx, float wz, float radius,
                      float opacity);

/* Same, but mapping by world_size directly (no mesh) - for chunked terrains. */
int g3d_terrain_paint_world(G3DPaintCanvas *c, G3DTexture *src, float src_tiling,
                            float world_size, float wx, float wz, float radius,
                            float opacity);

/* Upload the canvas pixels to its GL texture. */
void g3d_paint_upload(G3DPaintCanvas *c);

/* GL texture handle, to bind as a material albedo. */
unsigned int g3d_paint_get_gl(G3DPaintCanvas *c);

/* A G3DTexture* wrapper around the canvas GL texture, for g3d_material set. */
void *g3d_paint_get_texture(G3DPaintCanvas *c);

/* Save the canvas to a PNG file. Returns 1 on success. */
int g3d_paint_save(G3DPaintCanvas *c, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PAINT_H */
