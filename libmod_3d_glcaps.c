/*
 * libmod_3d_glcaps.c - Runtime GL capability detection and quality tiers
 *
 * Detects the context version (desktop GL or GLES) and the handful of features
 * the renderer actually branches on, then condenses them into a tier. See the
 * header for what each tier means.
 */

#include "libmod_3d_glcaps.h"
#include <stdio.h>
#include <string.h>

#ifdef VITA
#include "libmod_ray_vita_gl.h"
#else
#ifdef _WIN32
#include <GL/glew.h>
#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif

/* Constants that older/leaner GL headers may not declare. Guarded so this file
   compiles against a GLES2 or GL 2.1 header set even though the queries using
   them are themselves version-gated at runtime. */
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#endif

static G3DGLCaps g_caps;
static int g_caps_valid = 0;
static int g_max_tier = -1;      /* user cap; -1 = no cap */

/* ---------------------------------------------------------------------------
   Extension lookup
   --------------------------------------------------------------------------- */

#ifndef VITA
/* GL 3.0 deprecated the single GL_EXTENSIONS string in core profiles (it returns
   NULL there), so modern contexts must be walked with glGetStringi. Try the
   indexed form first and fall back to the old string for GL 2.x / GLES 2. */
static int gl_has_ext(const char *name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    if (glGetError() == GL_NO_ERROR && n > 0) {
        for (GLint i = 0; i < n; i++) {
            const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && strcmp(e, name) == 0) return 1;
        }
        return 0;
    }
    {
        const char *all = (const char *)glGetString(GL_EXTENSIONS);
        if (!all) return 0;
        /* substring match must land on a token boundary, or "GL_EXT_foo" would
           also match "GL_EXT_foobar" */
        size_t len = strlen(name);
        const char *p = all;
        while ((p = strstr(p, name)) != NULL) {
            char after = p[len];
            if ((p == all || p[-1] == ' ') && (after == ' ' || after == '\0'))
                return 1;
            p += len;
        }
        return 0;
    }
}
#endif

/* ---------------------------------------------------------------------------
   Detection
   --------------------------------------------------------------------------- */

static void glcaps_detect(void) {
    memset(&g_caps, 0, sizeof(g_caps));

#ifdef VITA
    /* Vita renders through VitaGL/GXM; treat it as the baseline tier. */
    g_caps.major = 2; g_caps.minor = 0; g_caps.es = 1;
    g_caps.max_texture_size = 1024;
    g_caps.vendor = "SCE"; g_caps.renderer = "GXM"; g_caps.version_string = "VitaGL";
    g_caps.native_tier = G3D_TIER_LOW;
#else
    const char *ver = (const char *)glGetString(GL_VERSION);
    g_caps.version_string = ver ? ver : "?";
    g_caps.vendor   = (const char *)glGetString(GL_VENDOR);
    g_caps.renderer = (const char *)glGetString(GL_RENDERER);
    if (!g_caps.vendor)   g_caps.vendor = "?";
    if (!g_caps.renderer) g_caps.renderer = "?";

    /* "OpenGL ES 3.2 v1.r26p0" vs "4.6.0 NVIDIA 580.105.08" */
    g_caps.es = (ver && strstr(ver, "OpenGL ES") != NULL) ? 1 : 0;

    /* GL_MAJOR_VERSION is itself GL 3.0+; on older contexts it raises
       GL_INVALID_ENUM and leaves the value untouched, so parse the string. */
    GLint mj = 0, mn = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &mj);
    glGetIntegerv(GL_MINOR_VERSION, &mn);
    if (glGetError() != GL_NO_ERROR || mj == 0) {
        const char *v = ver;
        if (v) {
            if (g_caps.es) { const char *d = strpbrk(v, "0123456789"); if (d) v = d; }
            if (sscanf(v, "%d.%d", &mj, &mn) != 2) { mj = 2; mn = 0; }
        } else { mj = 2; mn = 0; }
    }
    g_caps.major = (int)mj;
    g_caps.minor = (int)mn;

    int v = g_caps.major * 100 + g_caps.minor;   /* 403 = 4.3 */

    if (g_caps.es) {
        g_caps.compute         = (v >= 301) || gl_has_ext("GL_ARB_compute_shader");
        g_caps.tessellation    = (v >= 302) || gl_has_ext("GL_EXT_tessellation_shader")
                                            || gl_has_ext("GL_OES_tessellation_shader");
        g_caps.geometry_shader = (v >= 302) || gl_has_ext("GL_EXT_geometry_shader");
        g_caps.float_textures  = (v >= 300) || gl_has_ext("GL_OES_texture_float")
                                            || gl_has_ext("GL_OES_texture_half_float");
        g_caps.float_render    = (v >= 320) || gl_has_ext("GL_EXT_color_buffer_float")
                                            || gl_has_ext("GL_EXT_color_buffer_half_float");
        g_caps.texture_float_linear = (v >= 300) || gl_has_ext("GL_OES_texture_float_linear")
                                            || gl_has_ext("GL_OES_texture_half_float_linear");
        g_caps.instancing      = (v >= 300) || gl_has_ext("GL_EXT_draw_instanced");
        g_caps.depth_texture   = (v >= 300) || gl_has_ext("GL_OES_depth_texture");
        g_caps.texture_array   = (v >= 300);
        g_caps.npot            = (v >= 300) || gl_has_ext("GL_OES_texture_npot");
    } else {
        g_caps.compute         = (v >= 403) || gl_has_ext("GL_ARB_compute_shader");
        g_caps.tessellation    = (v >= 400) || gl_has_ext("GL_ARB_tessellation_shader");
        g_caps.geometry_shader = (v >= 302);
        g_caps.float_textures  = (v >= 300) || gl_has_ext("GL_ARB_texture_float");
        g_caps.float_render    = (v >= 300) || gl_has_ext("GL_ARB_color_buffer_float");
        g_caps.texture_float_linear = g_caps.float_textures;
        g_caps.instancing      = (v >= 303) || gl_has_ext("GL_ARB_draw_instanced");
        g_caps.depth_texture   = (v >= 300) || gl_has_ext("GL_ARB_depth_texture");
        g_caps.texture_array   = (v >= 300);
        g_caps.npot            = (v >= 200);
    }

    /* Compute needs image load/store to write its results anywhere useful. */
    if (g_caps.compute && v < 403 && !g_caps.es && !gl_has_ext("GL_ARB_shader_image_load_store"))
        g_caps.compute = 0;

    GLint tmp = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tmp);
    g_caps.max_texture_size = (int)tmp;
    tmp = 0; glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &tmp);
    if (glGetError() != GL_NO_ERROR) tmp = 1;
    g_caps.max_color_attachments = tmp > 0 ? (int)tmp : 1;
    if (g_caps.compute) {
        tmp = 0; glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &tmp);
        if (glGetError() != GL_NO_ERROR) tmp = 0;
        g_caps.max_compute_work_group_invocations = (int)tmp;
    }

    /* Tier = the weakest link, so that "tier >= X" implies every feature X needs.
       ULTRA deliberately demands BOTH compute and tessellation: GLES 3.1 has
       compute without tessellation and lands on MID, with its `compute` flag
       still set for code that only needs that one feature. */
    if (g_caps.compute && g_caps.tessellation && g_caps.float_render)
        g_caps.native_tier = G3D_TIER_ULTRA;
    else if (g_caps.tessellation && g_caps.float_render)
        g_caps.native_tier = G3D_TIER_HIGH;
    else if (g_caps.float_render && g_caps.depth_texture && g_caps.instancing)
        g_caps.native_tier = G3D_TIER_MID;
    else
        g_caps.native_tier = G3D_TIER_LOW;
#endif

    g_caps.tier = g_caps.native_tier;
    if (g_max_tier >= 0 && g_caps.tier > g_max_tier)
        g_caps.tier = g_max_tier;

    g_caps_valid = 1;
}

const G3DGLCaps *g3d_glcaps(void) {
    if (!g_caps_valid) glcaps_detect();
    return &g_caps;
}

void g3d_glcaps_refresh(void) {
    g_caps_valid = 0;
    glcaps_detect();
}

void g3d_glcaps_set_max_tier(int tier) {
    g_max_tier = (tier < 0) ? -1 : (tier > G3D_TIER_ULTRA ? G3D_TIER_ULTRA : tier);
    if (g_caps_valid) {
        g_caps.tier = g_caps.native_tier;
        if (g_max_tier >= 0 && g_caps.tier > g_max_tier)
            g_caps.tier = g_max_tier;
    }
}

int g3d_glcaps_get_max_tier(void) { return g_max_tier; }

const char *g3d_tier_name(int tier) {
    switch (tier) {
        case G3D_TIER_LOW:   return "low";
        case G3D_TIER_MID:   return "mid";
        case G3D_TIER_HIGH:  return "high";
        case G3D_TIER_ULTRA: return "ultra";
        default:             return "?";
    }
}

void g3d_glcaps_print(void) {
    const G3DGLCaps *c = g3d_glcaps();
    printf("G3D: GL %d.%d%s | %s | %s\n", c->major, c->minor,
           c->es ? " ES" : "", c->renderer, c->vendor);
    printf("G3D: tier=%s (hardware=%s)%s  compute=%d tess=%d floatRT=%d depthTex=%d\n",
           g3d_tier_name(c->tier), g3d_tier_name(c->native_tier),
           (c->tier != c->native_tier) ? " [capped]" : "",
           c->compute, c->tessellation, c->float_render, c->depth_texture);
}
