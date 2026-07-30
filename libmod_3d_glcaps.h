/*
 * libmod_3d_glcaps.h - Runtime GL capability detection and quality tiers
 *
 * One place that answers "what can this GPU actually do?", so features scale
 * instead of being all-or-nothing. The engine targets a wide range: a desktop
 * GL 4.6 card, a GLES 3.0 phone, and old fixed-ish hardware (Vita) all run the
 * same scenes, only with different rendering paths.
 *
 * The tier is a coarse summary for picking a rendering path; the individual
 * flags are there for finer decisions (e.g. "I only need float textures").
 * Everything is queried once, lazily, and cached.
 */

#ifndef __LIBMOD_3D_GLCAPS_H
#define __LIBMOD_3D_GLCAPS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Coarse capability tiers. Higher = strictly more capable: a tier N device
   supports everything every tier below it does, so a `tier >= X` test is always
   the right way to gate a feature. */
typedef enum {
    G3D_TIER_LOW   = 0,  /* GL 2.1 / GLES 2.0 / Vita: no float RT, no MRT       */
    G3D_TIER_MID   = 1,  /* GL 3.3 / GLES 3.0: float RTs, depth textures, VAOs  */
    G3D_TIER_HIGH  = 2,  /* GL 4.0 / GLES 3.2: + tessellation                   */
    G3D_TIER_ULTRA = 3,  /* GL 4.3 / GLES 3.1: + compute shaders, image store   */
} G3DTier;

typedef struct {
    int major, minor;        /* context version                                */
    int es;                  /* 1 = OpenGL ES context, 0 = desktop GL          */
    int tier;                /* G3DTier, after any user cap                    */
    int native_tier;         /* tier the hardware actually supports            */

    /* Individual features (1 = available). */
    int compute;             /* compute shaders + shader image load/store      */
    int tessellation;        /* tessellation control/evaluation stages         */
    int float_textures;      /* RGBA16F/32F sampling                           */
    int float_render;        /* ...and rendering into them                     */
    int texture_float_linear;/* linear filtering of float textures             */
    int instancing;
    int depth_texture;       /* sampling the depth buffer                      */
    int geometry_shader;
    int texture_array;
    int npot;                /* full non-power-of-two texture support          */

    int max_texture_size;
    int max_color_attachments;
    int max_compute_work_group_invocations;

    const char *vendor;
    const char *renderer;
    const char *version_string;
} G3DGLCaps;

/* Query (once) and return the capabilities of the CURRENT GL context. Must be
   called with a context bound; the result is cached, so call it freely. */
const G3DGLCaps *g3d_glcaps(void);

/* Re-query, e.g. after switching contexts. */
void g3d_glcaps_refresh(void);

/* Force a maximum tier regardless of hardware (quality setting / testing the
   fallback paths on a fast GPU). Pass -1 to remove the cap. Never raises the
   tier above what the hardware reports. */
void g3d_glcaps_set_max_tier(int tier);
int  g3d_glcaps_get_max_tier(void);

/* Human-readable tier name ("low"/"mid"/"high"/"ultra"). */
const char *g3d_tier_name(int tier);

/* Log the detected capabilities once (called by the renderer at init). */
void g3d_glcaps_print(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_GLCAPS_H */
