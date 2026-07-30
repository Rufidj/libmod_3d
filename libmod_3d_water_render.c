/*
 * libmod_3d_water_render.c - Drawing the unified water field
 *
 * The whole surface is ONE persistent grid over the world, plus ONE texture
 * describing the water (surface level, depth, flow) per cell. The grid is built
 * once; when the water moves, only the texture is re-uploaded. That is the
 * structural difference from the system this replaces, which rebuilt a CPU mesh
 * every single frame and so could never be either fast or smooth.
 *
 * Because the surface shape comes from a texture read in the vertex/evaluation
 * stage, sea, lakes and rivers are indistinguishable to the renderer -- they are
 * just different values in the same field.
 */

#include "libmod_3d_water_render.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_shaders.h"
#include "libmod_3d_water_spectrum.h"
#include "libmod_3d_water_falls.h"
#include "libmod_3d_glcaps.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_ibl.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef VITA
#ifdef _WIN32
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <SDL2/SDL.h>
#endif

#ifndef GL_PATCHES
#define GL_PATCHES 0x000E
#endif
#ifndef GL_PATCH_VERTICES
#define GL_PATCH_VERTICES 0x8E72
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

/* Base patch counts. Tessellation subdivides each patch further on the GPU, so
   the tessellated grid starts far coarser than the one that has to carry all of
   its detail in real vertices. */
#define WR_PATCHES_TESS 96
#define WR_PATCHES_FLAT 320

#ifndef VITA

static struct {
    int inited;
    int failed;                  /* shader build failed: stop retrying */

    G3DShaderProgram *prog;
    int use_tess;
    int force_tess;              /* -1 auto, 0 off, 1 on */

    unsigned int field_tex;
    int   tex_side;
    unsigned int field_rev;      /* field revision the texture reflects */
    float *stage;                /* CPU staging buffer for texture updates */
    float *level;                /* per-cell surface level, incl. dry shoreline */
    int   up_i0, up_j0, up_i1, up_j1;   /* rect uploaded last time; i1<i0 = none */

    unsigned int vao, vbo, ibo;
    int   index_count;
    int   grid_res;
    float grid_extent;
    float grid_origin_x, grid_origin_z;

    /* style */
    float amp, len, speed, choppy;
    float absorb[3], scatter[3];
    float rough, opacity, foam, refract;
    float sea_extent;
    int   want_spectrum;
    float spectrum_scale;
    float caustics;
    G3DShaderProgram *caustics_prog;
    unsigned int caustics_vao;
    int   caustics_failed;
    G3DShaderProgram *under_prog;
    int   under_failed;
    float under_visibility, under_shafts;
    G3DShaderProgram *swash_prog;
    int   swash_failed;
    float surf_amount, surf_freq, surf_speed, surf_runup;
    float surf_height, surf_dir_x, surf_dir_z;

    /* Persistent foam: two R16F fields, ping-ponged by a compute pass. */
    G3DShaderProgram *foam_prog;
    int   foam_failed;
    unsigned int foam_tex[2];
    int   foam_side;
    int   foam_cur;
    float foam_last_t;
    float foam_decay;
    float foam_max_cover;    /* techo: el tono del agua nunca se tapa del todo */
} R = {
    .want_spectrum = 1, .spectrum_scale = 1.0f, .caustics = 1.0f,
    .under_visibility = 28.0f, .under_shafts = 1.0f,
    /* Deliberately gentle out of the box: on a map whose ground sits just below
       the water line the whole field is one shallow shelf, and an aggressive
       breaker term paints all of it white. */
    .surf_amount = 0.55f, .surf_freq = 0.11f, .surf_speed = 0.16f, .surf_runup = 1.8f,
    .surf_height = 0.55f, .surf_dir_x = 1.0f, .surf_dir_z = 0.0f,
    .force_tess = -1,
    .amp = 0.22f, .len = 9.0f, .speed = 1.1f, .choppy = 1.0f,
    .absorb = { 0.45f, 0.09f, 0.045f },
    .scatter = { 0.06f, 0.20f, 0.24f },
    .rough = 0.06f, .opacity = 1.0f, .foam = 0.55f, .refract = 1.0f,
    .sea_extent = 4.0f,
    .foam_decay = 0.22f, .foam_max_cover = 0.78f,
};

/* --------------------------------------------------------------------------
   Shader assembly
   -------------------------------------------------------------------------- */

static char *wr_concat(const char *a, const char *b, const char *c) {
    size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
    char *s = (char *)malloc(n);
    if (!s) return NULL;
    strcpy(s, a); strcat(s, b); strcat(s, c);
    return s;
}

/* Build the #version + feature #define preamble for the detected tier. One
   shader body serves every tier; only this string changes. */
static void wr_preamble(char *out, size_t n, int tess, int frag) {
    const G3DGLCaps *c = g3d_glcaps();
    const char *version;
    if (c->es)                                  version = "#version 300 es\n";
    else if (c->major > 4 || (c->major == 4 && c->minor >= 3)) version = "#version 430 core\n";
    else if (c->major == 4)                     version = "#version 400 core\n";
    else                                        version = "#version 330 core\n";

    int ibl = (c->tier >= G3D_TIER_MID);
    int ssr = (c->tier >= G3D_TIER_HIGH);
    int spectrum = g3d_water_spectrum_ready() ? 1 : 0;

    snprintf(out, n,
             "%s%s#define G3D_TIER %d\n#define WATER_TESS %d\n"
             "#define WATER_IBL %d\n#define WATER_SSR %d\n#define WATER_SPECTRUM %d\n",
             version,
             (c->es && frag) ? "precision highp float;\nprecision highp sampler2D;\n"
                               "precision highp sampler2DArray;\n" : "",
             c->tier, tess ? 1 : 0, ibl, ssr, spectrum);
}

static int wr_build_shaders(void) {
    const G3DGLCaps *c = g3d_glcaps();
    int want_tess = (R.force_tess >= 0) ? R.force_tess
                                        : (c->tessellation && c->tier >= G3D_TIER_HIGH);

    char pre[320];
    if (want_tess) {
        char pv[320], pf[320];
        wr_preamble(pv, sizeof(pv), 1, 0);
        wr_preamble(pf, sizeof(pf), 1, 1);
        char *v = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_vert);
        char *tc = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_tcs);
        char *te = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_tes);
        char *f = wr_concat(pf, g3d_water_glsl_common, g3d_water_glsl_frag);
        if (v && tc && te && f)
            R.prog = g3d_shader_create_tess(v, tc, te, f);
        free(v); free(tc); free(te); free(f);
        if (R.prog) { R.use_tess = 1; return 1; }
        printf("G3D: water tessellation path failed, falling back to the flat grid\n");
    }

    wr_preamble(pre, sizeof(pre), 0, 0);
    char pf[320];
    wr_preamble(pf, sizeof(pf), 0, 1);
    char *v = wr_concat(pre, g3d_water_glsl_common, g3d_water_glsl_vert);
    char *f = wr_concat(pf, g3d_water_glsl_common, g3d_water_glsl_frag);
    if (v && f) R.prog = g3d_shader_create(v, f);
    free(v); free(f);
    R.use_tess = 0;
    return R.prog != NULL;
}

/* --------------------------------------------------------------------------
   The grid
   -------------------------------------------------------------------------- */

/* A flat lattice of quads over `extent` world units centred on the origin. With
   tessellation each quad is a 4-vertex patch the GPU subdivides; otherwise the
   quads are triangles and carry the detail themselves. */
static int wr_build_grid(float extent, int res, int tess) {
    if (res < 2) res = 2;
    int vside = res + 1;
    int nvert = vside * vside;

    float *verts = (float *)malloc((size_t)nvert * 3 * sizeof(float));
    if (!verts) return 0;
    float half = extent * 0.5f;
    float step = extent / (float)res;
    for (int j = 0; j < vside; j++) {
        for (int i = 0; i < vside; i++) {
            float *v = &verts[(j * vside + i) * 3];
            v[0] = -half + (float)i * step;
            v[1] = 0.0f;                       /* Y comes from the field texture */
            v[2] = -half + (float)j * step;
        }
    }

    int per = tess ? 4 : 6;
    int ncell = res * res;
    unsigned int *idx = (unsigned int *)malloc((size_t)ncell * per * sizeof(unsigned int));
    if (!idx) { free(verts); return 0; }
    int k = 0;
    for (int j = 0; j < res; j++) {
        for (int i = 0; i < res; i++) {
            unsigned int a = (unsigned int)(j * vside + i);
            unsigned int b = a + 1;
            unsigned int d = a + (unsigned int)vside;
            unsigned int e = d + 1;
            if (tess) {
                /* Corner order must match what the TCS assumes when it maps
                   edges to tessellation levels: 0-1-2-3 walking the quad. */
                idx[k++] = a; idx[k++] = b; idx[k++] = e; idx[k++] = d;
            } else {
                idx[k++] = a; idx[k++] = d; idx[k++] = b;
                idx[k++] = b; idx[k++] = d; idx[k++] = e;
            }
        }
    }

    if (!R.vao) glGenVertexArrays(1, &R.vao);
    if (!R.vbo) glGenBuffers(1, &R.vbo);
    if (!R.ibo) glGenBuffers(1, &R.ibo);

    glBindVertexArray(R.vao);
    glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nvert * 3 * sizeof(float), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ncell * per * sizeof(unsigned int),
                 idx, GL_STATIC_DRAW);
    glBindVertexArray(0);

    free(verts); free(idx);
    R.index_count = ncell * per;
    R.grid_res = res;
    R.grid_extent = extent;
    R.grid_origin_x = -half;
    R.grid_origin_z = -half;
    return 1;
}

/* --------------------------------------------------------------------------
   The field texture
   -------------------------------------------------------------------------- */

/* Per-cell surface level, including the DRY cells that border water.
 *
 * A dry cell must report the level of the water beside it, not its own ground
 * height. The texture is sampled with bilinear filtering, so a shore cell
 * holding its terrain height would drag the interpolated surface down into the
 * bank between the last wet sample and the first dry one -- the water would
 * visibly bend into the beach. Reporting the neighbouring water level instead
 * keeps the sheet flat right up to the shore, where the (higher, opaque)
 * terrain then hides its edge. Depth still goes to zero there, so the shader
 * fades the water out and discards it beyond. */
static void wr_compute_levels(int i0, int j0, int i1, int j1) {
    int S = g3d_waterfield_side();
    const float *d = g3d_waterfield_depth_array();
    const float *t = g3d_waterfield_terrain_array();

    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            int c = j * S + i;
            if (d[c] > 0.0f) { R.level[c] = t[c] + d[c]; continue; }
            float sum = 0.0f; int n = 0;
            for (int dj = -1; dj <= 1; dj++) {
                int nj = j + dj;
                if (nj < 0 || nj >= S) continue;
                for (int di = -1; di <= 1; di++) {
                    int ni = i + di;
                    if (ni < 0 || ni >= S) continue;
                    int nc = nj * S + ni;
                    if (d[nc] > 0.0f) { sum += t[nc] + d[nc]; n++; }
                }
            }
            R.level[c] = n ? (sum / (float)n) : t[c];
        }
    }
}

static int wr_ensure_field_texture(void) {
    int S = g3d_waterfield_side();
    if (S < 2) return 0;

    int recreate = (!R.field_tex || R.tex_side != S);
    if (recreate) {
        if (R.field_tex) { glDeleteTextures(1, &R.field_tex); R.field_tex = 0; }
    if (R.foam_tex[0]) { glDeleteTextures(2, R.foam_tex); R.foam_tex[0] = R.foam_tex[1] = 0; }
    R.foam_side = 0; R.foam_last_t = 0.0f;
    if (R.foam_prog) { g3d_shader_free(R.foam_prog); R.foam_prog = NULL; }
        free(R.stage); free(R.level);
        R.stage = (float *)malloc((size_t)S * S * 4 * sizeof(float));
        R.level = (float *)malloc((size_t)S * S * sizeof(float));
        if (!R.stage || !R.level) return 0;

        glGenTextures(1, &R.field_tex);
        glBindTexture(GL_TEXTURE_2D, R.field_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        /* Clamping matters: sampling past the edge must repeat the shoreline,
           not wrap the far side of the map into it. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, S, S, 0, GL_RGBA, GL_FLOAT, NULL);
        R.tex_side = S;
        R.field_rev = 0;
        R.up_i0 = 0; R.up_j0 = 0; R.up_i1 = -1; R.up_j1 = -1;
    }

    unsigned int rev = g3d_waterfield_revision();
    if (!recreate && rev == R.field_rev) return 1;
    R.field_rev = rev;

    const float *d = g3d_waterfield_depth_array();
    const float *vx, *vz;
    g3d_waterfield_flow_arrays(&vx, &vz);

    /* Only the wet region (plus a ring for the shoreline feather) can have
       changed. On a big map a river touches a few percent of the cells, so
       uploading just that rectangle is the difference between this scaling and
       not. The first upload after (re)creation has to cover everything. */
    int i0 = 0, j0 = 0, i1 = S - 1, j1 = S - 1;
    int have_wet = 1;
    if (!recreate) {
        have_wet = g3d_waterfield_wet_bounds(&i0, &j0, &i1, &j1);
        if (!have_wet) { i0 = j0 = 0; i1 = j1 = -1; }
        else { i0 -= 2; j0 -= 2; i1 += 2; j1 += 2; }

        /* Also refresh whatever we wrote LAST time. Water that recedes -- lower
           the sea level, drain a lake -- leaves cells outside the new wet box,
           and those texels would keep their old surface level forever, standing
           as a wall of stale water at the previous height next to the new one.
           Uploading the union of the old and new rectangles retires them. */
        if (R.up_i1 >= R.up_i0) {
            if (i1 < i0) { i0 = R.up_i0; j0 = R.up_j0; i1 = R.up_i1; j1 = R.up_j1; }
            else {
                if (R.up_i0 < i0) i0 = R.up_i0;
                if (R.up_j0 < j0) j0 = R.up_j0;
                if (R.up_i1 > i1) i1 = R.up_i1;
                if (R.up_j1 > j1) j1 = R.up_j1;
            }
        }
        if (i1 < i0 || j1 < j0) {          /* nothing wet and nothing stale */
            R.up_i0 = 0; R.up_j0 = 0; R.up_i1 = -1; R.up_j1 = -1;
            return 1;
        }
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > S - 1) i1 = S - 1;
        if (j1 > S - 1) j1 = S - 1;
    }
    /* Remember the wet part only: the margin is recomputed from the field each
       time anyway, and growing the remembered rect without bound would defeat
       the whole point of partial uploads. */
    if (have_wet && g3d_waterfield_wet_bounds(&R.up_i0, &R.up_j0, &R.up_i1, &R.up_j1)) {
        R.up_i0 -= 2; R.up_j0 -= 2; R.up_i1 += 2; R.up_j1 += 2;
    } else {
        R.up_i0 = 0; R.up_j0 = 0; R.up_i1 = -1; R.up_j1 = -1;
    }

    wr_compute_levels(i0, j0, i1, j1);

    int w = i1 - i0 + 1, h = j1 - j0 + 1;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int c = (j + j0) * S + (i + i0);
            float *p = &R.stage[(j * w + i) * 4];
            p[0] = R.level[c];
            p[1] = d[c];
            p[2] = vx[c];
            p[3] = vz[c];
        }
    }

    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, i0, j0, w, h, GL_RGBA, GL_FLOAT, R.stage);
    return 1;
}


/* --------------------------------------------------------------------------
   Caustics
   -------------------------------------------------------------------------- */

/* Full-screen additive pass that lights whatever the depth buffer says is under
   water. Done in screen space precisely so that nothing else -- terrain, models,
   the cave shader -- needs to know caustics exist. Needs the scene depth, so it
   is skipped when the renderer has not captured any. */

/* --------------------------------------------------------------------------
   Persistent foam

   Foam driven purely by the wave phase blinks in and out with the wave, which
   is one of the loudest tells that water is a shader. Real foam is left behind:
   the wave breaks, the raft of white stays on the surface, the current drags it
   and it dissolves over many seconds. So we keep a field of it and let a compute
   pass advect and decay it, topping it up wherever the surface is breaking.

   No compute (older GL, GLES 3.0)? The textures still exist and stay zero, so
   the surface simply falls back to the instantaneous foam it had before.
   -------------------------------------------------------------------------- */

#ifndef GL_R16F
#define GL_R16F  0x822D
#endif
#ifndef GL_RED
#define GL_RED   0x1903
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif

static int wr_ensure_foam_textures(int S) {
    if (R.foam_tex[0] && R.foam_side == S) return 1;
    if (R.foam_tex[0]) { glDeleteTextures(2, R.foam_tex); R.foam_tex[0] = R.foam_tex[1] = 0; }

    float *zero = (float *)calloc((size_t)S * S, sizeof(float));
    if (!zero) return 0;
    glGenTextures(2, R.foam_tex);
    for (int k = 0; k < 2; k++) {
        glBindTexture(GL_TEXTURE_2D, R.foam_tex[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, S, S, 0, GL_RED, GL_FLOAT, zero);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    free(zero);
    R.foam_side = S;
    R.foam_cur = 0;
    return 1;
}

/* Must run BEFORE the surface program is bound: it binds programs of its own,
   and uniforms set afterwards would land on the wrong one. */
static void wr_foam_update(float t) {
    int S = g3d_waterfield_side();
    if (S < 2) return;
    if (!wr_ensure_foam_textures(S)) return;

    const G3DGLCaps *c = g3d_glcaps();
    if (!c->compute || R.foam_failed) return;

    if (!R.foam_prog) {
        /* Compute needs its own #version -- the surface preamble may say 330. */
        char pre[320];
        wr_preamble(pre, sizeof(pre), 0, 0);
        const char *nl = strchr(pre, '\n');
        char comp[400];
        snprintf(comp, sizeof(comp), "%s%s",
                 c->es ? "#version 310 es\nprecision highp float;\n"
                         "precision highp sampler2D;\nprecision highp image2D;\n"
                       : "#version 430 core\n",
                 nl ? nl + 1 : "");
        char *src = wr_concat(comp, g3d_water_glsl_common, g3d_water_glsl_foam_comp);
        if (src) R.foam_prog = g3d_shader_create_compute(src);
        free(src);
        if (!R.foam_prog) {
            printf("G3D: persistent foam unavailable, using instantaneous foam\n");
            R.foam_failed = 1;
            return;
        }
    }

    /* Real seconds, clamped: a long stall must not teleport the foam across the
       map, and a paused editor must not decay it to nothing in one step. */
    float dt = (R.foam_last_t > 0.0f) ? (t - R.foam_last_t) : 0.016f;
    R.foam_last_t = t;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    if (dt <= 0.0f) return;

    int nxt = R.foam_cur ^ 1;
    G3DShaderProgram *sh = R.foam_prog;
    g3d_shader_use(sh);
    g3d_shader_set_int(sh, "uN", S);
    g3d_shader_set_float(sh, "uDt", dt);
    g3d_shader_set_float(sh, "uDecay", R.foam_decay);
    g3d_shader_set_float(sh, "uTime", t);
    g3d_shader_set_float(sh, "uSurfAmount", R.surf_amount);
    g3d_shader_set_float(sh, "uSurfFreq", R.surf_freq);
    g3d_shader_set_float(sh, "uSurfSpeed", R.surf_speed);
    g3d_shader_set_float(sh, "uSurfHeight", R.surf_height);
    g3d_shader_set_vec2(sh, "uSurfDir", R.surf_dir_x, R.surf_dir_z);
    float ffsize = g3d_waterfield_world_size();
    g3d_shader_set_vec4(sh, "uFieldOriginSize",
                        vec4_make(-ffsize * 0.5f, -ffsize * 0.5f, ffsize, ffsize));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    g3d_shader_set_int(sh, "uFieldTex", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, R.foam_tex[R.foam_cur]);
    g3d_shader_set_int(sh, "uFoamPrev", 1);
    glBindImageTexture(0, R.foam_tex[nxt], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);

    g3d_shader_dispatch(sh, (S + 15) / 16, (S + 15) / 16, 1);
    g3d_shader_image_barrier();

    glActiveTexture(GL_TEXTURE0);
    R.foam_cur = nxt;
}

int g3d_water_render_foam_readback(float *out, int *side) {
    if (!out || !R.foam_tex[0] || R.foam_side < 2) return 0;
    if (side) *side = R.foam_side;
    glBindTexture(GL_TEXTURE_2D, R.foam_tex[R.foam_cur]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, out);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

static void wr_caustics(G3DCamera *camera, Mat4 view, Mat4 proj, Mat4 invvp,
                        float t, int flip_y) {
    if (R.caustics <= 0.0f || R.caustics_failed) return;
    unsigned int dep = g3d_renderer_scene_depth_texture();
    if (!dep) return;

    if (!R.caustics_prog) {
        char pv[320], pf[320];
        wr_preamble(pv, sizeof(pv), 0, 0);
        wr_preamble(pf, sizeof(pf), 0, 1);
        char *v = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_caustics_vert);
        char *f = wr_concat(pf, g3d_water_glsl_common, g3d_water_glsl_caustics_frag);
        if (v && f) R.caustics_prog = g3d_shader_create(v, f);
        free(v); free(f);
        if (!R.caustics_prog) { R.caustics_failed = 1; return; }
        glGenVertexArrays(1, &R.caustics_vao);   /* empty VAO: the vertices are generated */
    }

    G3DShaderProgram *sh = R.caustics_prog;
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uInvViewProj", invvp);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);
    g3d_shader_set_float(sh, "uTime", t);
    g3d_shader_set_float(sh, "uStrength", R.caustics);
    g3d_shader_set_float(sh, "uFlipY", flip_y ? 1.0f : 0.0f);
    g3d_shader_set_vec3(sh, "uAbsorption", vec3_make(R.absorb[0], R.absorb[1], R.absorb[2]));

    float fsize = g3d_waterfield_world_size();
    g3d_shader_set_vec4(sh, "uFieldOriginSize",
                        vec4_make(-fsize * 0.5f, -fsize * 0.5f, fsize, fsize));
    g3d_shader_set_float(sh, "uSeaLevel", g3d_waterfield_get_sea_level());

    {
        float sd[3] = { 0 }, sc[3] = { 0 };
        g3d_sky_get_sun(sd, sc);
        float slen = sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2];
        if (slen < 1e-6f) { sd[0] = 0.35f; sd[1] = 0.82f; sd[2] = 0.45f; slen = 1.0f; }
        float inv = 1.0f / sqrtf(sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2]);
        if (sc[0] + sc[1] + sc[2] < 1e-4f) { sc[0] = 1.0f; sc[1] = 0.97f; sc[2] = 0.92f; }
        g3d_shader_set_vec3(sh, "uSunDir", vec3_make(sd[0]*inv, sd[1]*inv, sd[2]*inv));
        g3d_shader_set_vec3(sh, "uSunColor", vec3_make(sc[0], sc[1], sc[2]));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    g3d_shader_set_int(sh, "uFieldTex", 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, dep);
    g3d_shader_set_int(sh, "uDepthTex", 2);
    if (g3d_water_spectrum_ready()) {
        g3d_water_spectrum_bind(3, 4);
        g3d_shader_set_int(sh, "uDisplacement", 3);
        g3d_shader_set_int(sh, "uDerivative", 4);
        float tiles[G3D_WATER_CASCADES];
        g3d_water_spectrum_tile_sizes(tiles);
        g3d_shader_set_vec4(sh, "uTileSizes",
                            vec4_make(tiles[0], tiles[1], tiles[2], 0.0f));
        g3d_shader_set_float(sh, "uSpectrumScale", R.spectrum_scale);
    }

    /* Light is added, never removed: caustics brighten, they do not tint. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(R.caustics_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    /* Hand the state back exactly as it was found. Leaving the depth mask off
       silently breaks every later pass that wants to write depth -- including
       the next frame's glClear, which honours the write mask. */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    (void)view; (void)proj;
}


/* Volumetric underwater. Only while the camera itself is submerged: above the
   surface the water shader has already absorbed everything seen through it, and
   running both would apply the same extinction twice. */
static void wr_underwater(G3DCamera *camera, Mat4 invvp, float t, int flip_y) {
    if (R.under_failed || !g3d_waterfield_active()) return;
    float lvl = g3d_waterfield_level_at(camera->position.x, camera->position.z);
    if (lvl < G3D_NO_WATER_TEST || camera->position.y >= lvl) return;
    unsigned int dep = g3d_renderer_scene_depth_texture();
    if (!dep) return;

    if (!R.under_prog) {
        char pv[320], pf[320];
        wr_preamble(pv, sizeof(pv), 0, 0);
        wr_preamble(pf, sizeof(pf), 0, 1);
        char *v = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_caustics_vert);
        char *f = wr_concat(pf, g3d_water_glsl_common, g3d_water_glsl_underwater_frag);
        if (v && f) R.under_prog = g3d_shader_create(v, f);
        free(v); free(f);
        if (!R.under_prog) { R.under_failed = 1; return; }
        if (!R.caustics_vao) glGenVertexArrays(1, &R.caustics_vao);
    }

    G3DShaderProgram *sh = R.under_prog;
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uInvViewProj", invvp);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);
    g3d_shader_set_float(sh, "uTime", t);
    g3d_shader_set_float(sh, "uFlipY", flip_y ? 1.0f : 0.0f);
    g3d_shader_set_float(sh, "uVisibility", R.under_visibility);
    g3d_shader_set_float(sh, "uShafts", R.under_shafts);
    g3d_shader_set_vec3(sh, "uAbsorption", vec3_make(R.absorb[0], R.absorb[1], R.absorb[2]));
    g3d_shader_set_vec3(sh, "uScatterColor", vec3_make(R.scatter[0], R.scatter[1], R.scatter[2]));
    {
        float sd[3] = { 0 }, sc[3] = { 0 };
        g3d_sky_get_sun(sd, sc);
        float sl = sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2];
        if (sl < 1e-6f) { sd[0] = 0.35f; sd[1] = 0.82f; sd[2] = 0.45f; }
        float inv = 1.0f / sqrtf(sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2]);
        if (sc[0] + sc[1] + sc[2] < 1e-4f) { sc[0] = 1.0f; sc[1] = 0.97f; sc[2] = 0.92f; }
        g3d_shader_set_vec3(sh, "uSunDir", vec3_make(sd[0]*inv, sd[1]*inv, sd[2]*inv));
        g3d_shader_set_vec3(sh, "uSunColor", vec3_make(sc[0], sc[1], sc[2]));
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, dep);
    g3d_shader_set_int(sh, "uDepthTex", 2);
    float fsize = g3d_waterfield_world_size();
    g3d_shader_set_vec4(sh, "uFieldOriginSize",
                        vec4_make(-fsize * 0.5f, -fsize * 0.5f, fsize, fsize));
    g3d_shader_set_float(sh, "uSeaLevel", g3d_waterfield_get_sea_level());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    g3d_shader_set_int(sh, "uFieldTex", 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(R.caustics_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}


/* The swash sheet running up the sand. Screen space for the same reason the
   caustics are: the water surface does not exist over dry ground, so the only
   way to put water there is to work from what the depth buffer already drew. */
static void wr_swash(G3DCamera *camera, Mat4 invvp, float t, int flip_y) {
    if (R.swash_failed || R.surf_amount <= 0.0f || R.surf_runup <= 0.0f) return;
    if (!g3d_waterfield_active()) return;
    unsigned int dep = g3d_renderer_scene_depth_texture();
    if (!dep) return;

    if (!R.swash_prog) {
        char pv[320], pf[320];
        wr_preamble(pv, sizeof(pv), 0, 0);
        wr_preamble(pf, sizeof(pf), 0, 1);
        char *v = wr_concat(pv, g3d_water_glsl_common, g3d_water_glsl_caustics_vert);
        char *f = wr_concat(pf, g3d_water_glsl_common, g3d_water_glsl_swash_frag);
        if (v && f) R.swash_prog = g3d_shader_create(v, f);
        free(v); free(f);
        if (!R.swash_prog) { R.swash_failed = 1; return; }
        if (!R.caustics_vao) glGenVertexArrays(1, &R.caustics_vao);
    }

    G3DShaderProgram *sh = R.swash_prog;
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uInvViewProj", invvp);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);
    g3d_shader_set_float(sh, "uTime", t);
    g3d_shader_set_float(sh, "uFlipY", flip_y ? 1.0f : 0.0f);
    g3d_shader_set_float(sh, "uSurfAmount", R.surf_amount);
    g3d_shader_set_float(sh, "uSurfFreq", R.surf_freq);
    g3d_shader_set_float(sh, "uSurfSpeed", R.surf_speed);
    g3d_shader_set_float(sh, "uSurfRunup", R.surf_runup);
    g3d_shader_set_float(sh, "uSurfHeight", R.surf_height);
    g3d_shader_set_vec2(sh, "uSurfDir", R.surf_dir_x, R.surf_dir_z);
    {
        float sd[3] = { 0 }, sc[3] = { 0 }, amb[3] = { 0 };
        g3d_sky_get_sun(sd, sc);
        g3d_sky_get_ambient(amb);
        if (sc[0] + sc[1] + sc[2] < 1e-4f) { sc[0] = 1.0f; sc[1] = 0.97f; sc[2] = 0.92f; }
        if (amb[0] + amb[1] + amb[2] < 1e-4f) { amb[0] = 0.22f; amb[1] = 0.27f; amb[2] = 0.33f; }
        g3d_shader_set_vec3(sh, "uSunColor", vec3_make(sc[0], sc[1], sc[2]));
        g3d_shader_set_vec3(sh, "uAmbient", vec3_make(amb[0], amb[1], amb[2]));
    }
    float fsize = g3d_waterfield_world_size();
    g3d_shader_set_vec4(sh, "uFieldOriginSize",
                        vec4_make(-fsize * 0.5f, -fsize * 0.5f, fsize, fsize));
    g3d_shader_set_float(sh, "uSeaLevel", g3d_waterfield_get_sea_level());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    g3d_shader_set_int(sh, "uFieldTex", 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, dep);
    g3d_shader_set_int(sh, "uDepthTex", 2);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(R.caustics_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}

/* --------------------------------------------------------------------------
   Render
   -------------------------------------------------------------------------- */

void g3d_water_render(G3DCamera *camera, int flip_y) {
    if (!camera || R.failed) return;
    if (!g3d_waterfield_active()) return;

    if (!R.inited) {
        /* Spectral waves where the hardware can run them. This must happen
           BEFORE the shaders are built, because whether the spectrum exists is
           compiled into them as WATER_SPECTRUM. */
        if (R.want_spectrum && g3d_glcaps()->compute && !g3d_water_spectrum_ready())
            g3d_water_spectrum_init(256);

        if (!wr_build_shaders()) {
            fprintf(stderr, "G3D: water shader build failed; water disabled\n");
            R.failed = 1;
            return;
        }
        R.inited = 1;
        printf("G3D: water renderer ready (tier=%s, %s grid)\n",
               g3d_tier_name(g3d_glcaps()->tier),
               R.use_tess ? "tessellated" : "flat");
    }
    if (!wr_ensure_field_texture()) return;

    /* The grid spans the terrain, or further out when a sea is set, so the
       ocean reaches past the shore instead of stopping at the map edge. */
    float fsize = g3d_waterfield_world_size();
    float sea = g3d_waterfield_get_sea_level();
    float extent = (sea > G3D_NO_WATER_TEST) ? fsize * R.sea_extent : fsize;
    int res = R.use_tess ? WR_PATCHES_TESS : WR_PATCHES_FLAT;
    if (!R.vao || R.grid_extent != extent || R.grid_res != res) {
        if (!wr_build_grid(extent, res, R.use_tess)) return;
    }

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }
    Mat4 viewproj = mat4_multiply(proj, view);
    Mat4 invvp = mat4_inverse(viewproj);

    float t = (float)SDL_GetTicks() / 1000.0f;

    /* Advance the FFT cascades BEFORE binding the surface program. The compute
       passes bind their own programs, so running them mid-setup would leave a
       compute shader active and send every uniform after this point to the wrong
       program -- which silently draws nothing at all. */
    if (g3d_water_spectrum_ready())
        g3d_water_spectrum_update(t);

    /* Same rule for the foam pass, and it must come before the surface reads
       the field it just wrote. */
    wr_foam_update(t);

    G3DShaderProgram *sh = R.prog;
    g3d_shader_use(sh);

    g3d_shader_set_mat4(sh, "uView", view);
    g3d_shader_set_mat4(sh, "uProjection", proj);
    g3d_shader_set_mat4(sh, "uViewProj", viewproj);
    g3d_shader_set_mat4(sh, "uInvViewProj", invvp);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);

    g3d_shader_set_float(sh, "uTime", t);
    g3d_shader_set_float(sh, "uWaveAmp", R.amp);
    g3d_shader_set_float(sh, "uWaveLen", R.len);
    g3d_shader_set_float(sh, "uWaveSpeed", R.speed);
    g3d_shader_set_float(sh, "uChoppy", R.choppy);
    g3d_shader_set_float(sh, "uSurfAmount", R.surf_amount);
    g3d_shader_set_float(sh, "uSurfFreq", R.surf_freq);
    g3d_shader_set_float(sh, "uSurfSpeed", R.surf_speed);
    g3d_shader_set_float(sh, "uSurfRunup", R.surf_runup);
    g3d_shader_set_float(sh, "uSurfHeight", R.surf_height);
    g3d_shader_set_vec2(sh, "uSurfDir", R.surf_dir_x, R.surf_dir_z);

    /* Field placement. The shader turns a world XZ into a texture UV with this,
       so it must match how the field itself is laid out: centred on the origin. */
    g3d_shader_set_vec4(sh, "uFieldOriginSize",
                        vec4_make(-fsize * 0.5f, -fsize * 0.5f, fsize, fsize));
    g3d_shader_set_float(sh, "uSeaLevel", sea);

    g3d_shader_set_vec3(sh, "uAbsorption", vec3_make(R.absorb[0], R.absorb[1], R.absorb[2]));
    g3d_shader_set_vec3(sh, "uScatterColor", vec3_make(R.scatter[0], R.scatter[1], R.scatter[2]));
    g3d_shader_set_float(sh, "uRoughness", R.rough);
    g3d_shader_set_float(sh, "uOpacity", R.opacity);
    g3d_shader_set_float(sh, "uFoamAmount", R.foam);
    g3d_shader_set_float(sh, "uFoamMaxCover", R.foam_max_cover);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, R.foam_tex[R.foam_cur]);
    g3d_shader_set_int(sh, "uFoamTex", 7);
    glActiveTexture(GL_TEXTURE0);
    g3d_shader_set_float(sh, "uRefractStrength", R.refract);

    {   /* Sun + ambient from the sky, so the water is lit like everything else.
         *
         * A scene that never configured a sky reports zero for all of it, and
         * water lit by nothing is not merely dark: the foam term multiplies the
         * light colour, so crests and shorelines paint pure BLACK over the
         * surface. Falling back to a plain overhead sun keeps such scenes
         * looking like water instead of tar. */
        float sd[3] = { 0.0f, 0.0f, 0.0f }, sc[3] = { 0.0f, 0.0f, 0.0f };
        float amb[3] = { 0.0f, 0.0f, 0.0f };
        g3d_sky_get_sun(sd, sc);
        g3d_sky_get_ambient(amb);

        float slen = sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2];
        if (slen < 1e-6f) {
            sd[0] = 0.35f; sd[1] = 0.82f; sd[2] = 0.45f;
            slen = sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2];
        }
        float inv = 1.0f / sqrtf(slen);
        sd[0] *= inv; sd[1] *= inv; sd[2] *= inv;

        if (sc[0] + sc[1] + sc[2] < 1e-4f) {
            sc[0] = 1.0f; sc[1] = 0.97f; sc[2] = 0.92f;
        }
        if (amb[0] + amb[1] + amb[2] < 1e-4f) {
            amb[0] = 0.22f; amb[1] = 0.27f; amb[2] = 0.33f;
        }

        g3d_shader_set_vec3(sh, "uSunDir", vec3_make(sd[0], sd[1], sd[2]));
        g3d_shader_set_vec3(sh, "uSunColor", vec3_make(sc[0], sc[1], sc[2]));
        g3d_shader_set_vec3(sh, "uAmbient", vec3_make(amb[0], amb[1], amb[2]));
    }

    {   /* same fog as the world, or distant water floats on the horizon */
        int fen = 0; Vec3 fcol = vec3_make(0.7f, 0.78f, 0.88f); float fst = 0.0f, fnd = 1.0f;
        g3d_renderer_get_fog(&fen, &fcol, &fst, &fnd);
        g3d_shader_set_int(sh, "uFogEnabled", fen);
        g3d_shader_set_vec3(sh, "uFogColor", fcol);
        g3d_shader_set_float(sh, "uFogStart", fst);
        g3d_shader_set_float(sh, "uFogEnd", fnd);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, R.field_tex);
    g3d_shader_set_int(sh, "uFieldTex", 0);

    unsigned int scn = g3d_renderer_scene_texture();
    unsigned int dep = g3d_renderer_scene_depth_texture();
    if (scn && dep) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, scn);
        g3d_shader_set_int(sh, "uSceneTex", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, dep);
        g3d_shader_set_int(sh, "uDepthTex", 2);
        g3d_shader_set_int(sh, "uHasScene", 1);
    } else {
        g3d_shader_set_int(sh, "uHasScene", 0);
    }

    if (g3d_glcaps()->tier >= G3D_TIER_MID) {
        int has_ibl = g3d_ibl_enabled() ? g3d_ibl_bind(5, 6, 7) : 0;
        g3d_shader_set_int(sh, "uHasIBL", has_ibl);
        if (has_ibl) {
            g3d_shader_set_int(sh, "uPrefilter", 6);
            g3d_shader_set_float(sh, "uPrefilterMips", g3d_ibl_prefilter_mips());
        }
    }

    if (g3d_water_spectrum_ready()) {
        /* Binding only -- the cascades were advanced before this program was
           made current, because the compute passes swap the active program. */
        int n = g3d_water_spectrum_bind(3, 4);
        if (n > 0) {
            g3d_shader_set_int(sh, "uDisplacement", 3);
            g3d_shader_set_int(sh, "uDerivative", 4);
            float tiles[G3D_WATER_CASCADES];
            g3d_water_spectrum_tile_sizes(tiles);
            g3d_shader_set_vec4(sh, "uTileSizes",
                                vec4_make(tiles[0], tiles[1], tiles[2], 0.0f));
            g3d_shader_set_float(sh, "uSpectrumScale", R.spectrum_scale);
        }
    }

    if (R.use_tess) {
        g3d_shader_set_float(sh, "uTessNear", 24.0f);
        g3d_shader_set_float(sh, "uTessFar", 320.0f);
        g3d_shader_set_float(sh, "uTessMax", 14.0f);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    /* Transparent: test against the world but do not occlude what comes after. */
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(R.vao);
    if (R.use_tess) {
        glPatchParameteri(GL_PATCH_VERTICES, 4);
        glDrawElements(GL_PATCHES, R.index_count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawElements(GL_TRIANGLES, R.index_count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);

    /* Falling water goes on top of the surface it fell from, so it has to come
       after: the sheet is transparent and must blend over the pool below. */
    g3d_water_falls_render(camera, flip_y);

    /* Caustics last: they light the sea bed, which is already on screen. */
    wr_caustics(camera, view, proj, invvp, t, flip_y);

    /* The swash goes over the sand the caustics just lit. */
    wr_swash(camera, invvp, t, flip_y);

    /* Last of all: the water the camera is actually sitting in, which fogs
       everything already drawn -- caustics included. */
    wr_underwater(camera, invvp, t, flip_y);
}

/* --------------------------------------------------------------------------
   Tuning
   -------------------------------------------------------------------------- */

void g3d_water_render_set_waves(float amplitude, float wavelength, float speed,
                                float choppy) {
    R.amp = amplitude < 0.0f ? 0.0f : amplitude;
    R.len = wavelength > 0.05f ? wavelength : 0.05f;
    R.speed = speed;
    R.choppy = choppy < 0.0f ? 0.0f : (choppy > 2.0f ? 2.0f : choppy);
}

void g3d_water_render_set_optics(float ar, float ag, float ab,
                                 float sr, float sg, float sb,
                                 float roughness, float opacity) {
    R.absorb[0] = ar < 0.0f ? 0.0f : ar;
    R.absorb[1] = ag < 0.0f ? 0.0f : ag;
    R.absorb[2] = ab < 0.0f ? 0.0f : ab;
    R.scatter[0] = sr; R.scatter[1] = sg; R.scatter[2] = sb;
    R.rough = roughness < 0.001f ? 0.001f : roughness;
    R.opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
}

void g3d_water_render_set_detail(float foam, float refraction) {
    R.foam = foam < 0.0f ? 0.0f : foam;
    R.refract = refraction < 0.0f ? 0.0f : refraction;
}

void g3d_water_render_set_surf(float amount, float wavelength, float speed,
                               float runup) {
    R.surf_amount = amount < 0.0f ? 0.0f : amount;
    /* The phase runs on DEPTH, so "wavelength" is how many units of depth one
       band spans -- a gentler beach therefore shows wider bands, which is what
       a real one does. */
    R.surf_freq = wavelength > 0.05f ? (1.0f / wavelength) : 0.11f;
    R.surf_speed = speed;
    R.surf_runup = runup < 0.0f ? 0.0f : runup;
}

void g3d_water_render_set_surf_wave(float height, float direction_deg) {
    R.surf_height = height < 0.0f ? 0.0f : height;
    /* Stored as a unit vector: the shader dots it with a world position every
       pixel, so the trigonometry belongs here, once. */
    float a = direction_deg * 0.01745329252f;
    R.surf_dir_x = cosf(a);
    R.surf_dir_z = sinf(a);
}

void g3d_water_render_set_underwater(float visibility, float shafts) {
    R.under_visibility = visibility > 1.0f ? visibility : 1.0f;
    R.under_shafts = shafts < 0.0f ? 0.0f : shafts;
}

void g3d_water_render_set_caustics(float strength) {
    R.caustics = strength < 0.0f ? 0.0f : strength;
}
float g3d_water_render_get_caustics(void) { return R.caustics; }

void g3d_water_render_force_tessellation(int mode) {
    if (mode == R.force_tess) return;
    R.force_tess = mode;
    /* Rebuild on the next frame: the grid topology differs between the paths. */
    if (R.prog) { g3d_shader_free(R.prog); R.prog = NULL; }
    R.inited = 0;
    R.failed = 0;
    R.grid_res = 0;
}

void g3d_water_render_set_sea_extent(float multiple) {
    R.sea_extent = multiple < 1.0f ? 1.0f : multiple;
}

void g3d_water_render_shutdown(void) {
    if (R.prog) { g3d_shader_free(R.prog); R.prog = NULL; }
    if (R.caustics_prog) { g3d_shader_free(R.caustics_prog); R.caustics_prog = NULL; }
    if (R.under_prog) { g3d_shader_free(R.under_prog); R.under_prog = NULL; }
    if (R.swash_prog) { g3d_shader_free(R.swash_prog); R.swash_prog = NULL; }
    R.swash_failed = 0;
    R.under_failed = 0;
    if (R.caustics_vao) { glDeleteVertexArrays(1, &R.caustics_vao); R.caustics_vao = 0; }
    R.caustics_failed = 0;
    if (R.field_tex) { glDeleteTextures(1, &R.field_tex); R.field_tex = 0; }
    if (R.vbo) { glDeleteBuffers(1, &R.vbo); R.vbo = 0; }
    if (R.ibo) { glDeleteBuffers(1, &R.ibo); R.ibo = 0; }
    if (R.vao) { glDeleteVertexArrays(1, &R.vao); R.vao = 0; }
    free(R.stage); R.stage = NULL;
    free(R.level); R.level = NULL;
    R.inited = 0; R.failed = 0; R.tex_side = 0; R.grid_res = 0;
}

#else /* VITA: no GL path yet */

void g3d_water_render(G3DCamera *camera, int flip_y) { (void)camera; (void)flip_y; }
void g3d_water_render_set_waves(float a, float w, float s, float c) { (void)a; (void)w; (void)s; (void)c; }
void g3d_water_render_set_optics(float ar, float ag, float ab, float sr, float sg,
                                 float sb, float r, float o) {
    (void)ar; (void)ag; (void)ab; (void)sr; (void)sg; (void)sb; (void)r; (void)o;
}
void g3d_water_render_set_detail(float f, float r) { (void)f; (void)r; }
void g3d_water_render_force_tessellation(int m) { (void)m; }
void g3d_water_render_set_sea_extent(float m) { (void)m; }
void g3d_water_render_shutdown(void) {}

#endif
