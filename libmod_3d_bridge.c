/*
 * libmod_3d_bridge.c - BennuGD2 <-> libmod_3d render bridge
 *
 * Renders the 3D scene into a BennuGD GRAPH's SDL_gpu texture by binding the
 * GRAPH's underlying OpenGL FBO, running the raw-GL forward renderer into it,
 * and then restoring SDL_gpu's state so BennuGD can composite the GRAPH on
 * screen as usual.
 *
 * This file is isolated from the raw-GL renderer so the SDL_gpu / libbggfx
 * headers don't clash with <GL/gl.h>.
 */

#include "g_bitmap.h"   /* GRAPH */
#include "g_grlib.h"    /* bitmap_get, bitmap_new_syslib */
#include "SDL_gpu.h"    /* GPU_Image, GPU_Target, GPU_* helpers */
#include "libmod_3d_renderer.h"
#include <stdio.h>

/* Raw-GL frame render (defined in libmod_3d.c) */
extern int g3d_engine_render(void);

/* SDL_gpu OpenGL backend target data: the FBO id is the 2nd field. This layout
   is shared by the OpenGL_2 and OpenGL_3 backends, so we read it directly
   instead of pulling in a backend-specific header. */
typedef struct {
    int refcount;
    Uint32 handle;  /* GL framebuffer object id */
    Uint32 format;
} G3DTargetDataGL;

/*
 * Render the active 3D scene into the given GRAPH.
 *   graph_id == 0 -> a new system GRAPH is allocated and its id returned.
 *   graph_id  > 0 -> render into that existing GRAPH.
 * Returns the GRAPH id used, or -1 on failure.
 */
int g3d_bridge_render_to_graph(int64_t graph_id) {
    uint32_t w = 0, h = 0;
    g3d_renderer_get_display_size(&w, &h);
    if (w == 0 || h == 0)
        return -1;

    GRAPH *dest = NULL;
    if (graph_id == 0) {
        dest = bitmap_new_syslib((int64_t)w, (int64_t)h);
        if (!dest)
            return -1;
        graph_id = dest->code;
    } else {
        dest = bitmap_get(0, graph_id);
        if (!dest)
            return -1;
    }

    /* Ensure the GRAPH has a GPU image */
    GPU_Image *image = (GPU_Image *)dest->tex;
    if (!image) {
        image = GPU_CreateImage(dest->width, dest->height, GPU_FORMAT_RGBA);
        if (!image)
            return -1;
        dest->tex = image;
    }

    /* Ensure the image has a render target (FBO) with a depth buffer */
    GPU_Target *target = image->target;
    if (!target) {
        target = GPU_LoadTarget(image);
        if (!target)
            return -1;
        GPU_AddDepthBuffer(target);
    }

    Uint32 fbo = ((G3DTargetDataGL *)target->data)->handle;

    /* Flush SDL_gpu's pending draws so the FBO is in a clean state, hand the
       context over to the raw-GL renderer, then restore SDL_gpu afterwards. */
    GPU_FlushBlitBuffer();

    g3d_renderer_set_target(fbo);
    g3d_engine_render();
    g3d_renderer_set_target(0);

    GPU_ResetRendererState();

    return (int)graph_id;
}
