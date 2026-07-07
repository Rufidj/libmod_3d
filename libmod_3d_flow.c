/*
 * libmod_3d_flow.c - Flowing water (waterfalls / rivers) implementation
 */

#include "libmod_3d_flow.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_chunkterrain.h"   /* g3d_heightfield_height */
#include "libmod_3d_scene.h"
#include "libmod_3d_light.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define G3D_MAX_FLOWS 16
#define FLOW_COLS 8
#define FLOW_ROWS 24

/* ---- shaders ----------------------------------------------------------- */

static const char *flow_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 uv;\n"  /* normalized 0..1 (x across, y down) */
    "layout(location = 2) in float turb;\n" /* per-vertex turbulence (steepness) */
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "out vec2 vUV;\n"
    "out float vTurb;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    vUV = uv;\n"
    "    vTurb = turb;\n"
    "    vWorldPos = position;\n"
    "    gl_Position = uProjection * uView * vec4(position, 1.0);\n"
    "}\n";

static const char *flow_frag =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in float vTurb;\n"
    "in vec3 vWorldPos;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uHasTex;\n"
    "uniform float uTime;\n"
    "uniform float uSpeed;\n"
    "uniform float uTiling;\n"
    "uniform vec3 uColor;\n"          /* deep water colour */
    "uniform vec3 uCameraPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uLightColor;\n"
    "uniform int uClipOn;\n"
    "uniform float uClipY;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    if (uClipOn == 1 && vWorldPos.y < uClipY) discard;\n"
    "    /* Water surface normal (up), bumped by the scrolling texture for ripples */\n"
    "    vec3 N = vec3(0.0, 1.0, 0.0);\n"
    "    float detail = 0.5;\n"
    "    if (uHasTex == 1) {\n"
    "        vec2 a = vec2(vUV.x * 2.0, vUV.y * uTiling - uTime * uSpeed);\n"
    "        vec2 b = vec2(vUV.x * 2.0 + 0.3, vUV.y * uTiling * 1.7 - uTime * uSpeed * 1.5);\n"
    "        vec3 t1 = texture(uTex, a).rgb;\n"
    "        vec3 t2 = texture(uTex, b).rgb;\n"
    "        detail = mix(dot(t1, vec3(0.3333)), dot(t2, vec3(0.3333)), 0.5);\n"
    "        N = normalize(N + vec3((t1.r - 0.5) * 0.8, 0.0, (t2.b - 0.5) * 0.8));\n"
    "    }\n"
    "    /* Sea-like shading: Fresnel + sun specular, deep->shallow by view angle */\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    "    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);\n"
    "    vec3 L = normalize(-uLightDir);\n"
    "    vec3 R = reflect(-L, N);\n"
    "    float spec = pow(max(dot(R, V), 0.0), 90.0);\n"
    "    vec3 deep = uColor;\n"
    "    vec3 shallow = clamp(uColor * 1.6 + 0.15, 0.0, 1.0);\n"
    "    vec3 base = mix(deep, shallow, fres) * (0.8 + 0.4 * detail);\n"
    "    /* Whitewater foam only where it's turbulent (steep drops) */\n"
    "    float foam = smoothstep(0.55, 1.0, vTurb) * (0.4 + 0.6 * detail);\n"
    "    foam = clamp(foam, 0.0, 1.0);\n"
    "    vec3 col = mix(base, vec3(1.0), foam) + uLightColor * spec * 0.9;\n"
    "    float edge = smoothstep(0.0, 0.08, vUV.x) * smoothstep(1.0, 0.92, vUV.x);\n"
    "    float alpha = mix(0.6, 0.95, max(fres, foam)) * (0.5 + 0.5 * edge);\n"
    "    FragColor = vec4(col, alpha);\n"
    "}\n";

/* ---- state ------------------------------------------------------------- */

typedef struct {
    unsigned int vao, vbo, ebo;
    int index_count;
    float speed;
    float tiling;
    int active;
} FlowQuad;

static struct {
    int initialized;
    G3DShaderProgram *shader;
    unsigned int tex_handle;
    float color[3];
    int clip_on;
    float clip_y;
    FlowQuad quads[G3D_MAX_FLOWS];
    int count;
} g_flow = {0};

void g3d_flow_set_clip(float y) {
    g_flow.clip_on = 1;
    g_flow.clip_y = y;
}

static void flow_lazy_init(void) {
    if (g_flow.initialized)
        return;
    g_flow.shader = g3d_shader_create(flow_vert, flow_frag);
    g_flow.color[0] = 0.6f; g_flow.color[1] = 0.78f; g_flow.color[2] = 0.85f;
    g_flow.initialized = 1;
}

void g3d_flow_set_texture(unsigned int gl_handle) {
    g_flow.tex_handle = gl_handle;
}

void g3d_flow_set_color(float r, float g, float b) {
    g_flow.color[0] = r; g_flow.color[1] = g; g_flow.color[2] = b;
}

#ifndef VITA
/* Upload an interleaved (xyz, uv, turb) vertex grid as a flow quad. Returns id. */
static int flow_register(const float *vdata, int vcount, int cols, int rows,
                         float speed, float tiling) {
    if (g_flow.count >= G3D_MAX_FLOWS)
        return -1;
    int vcols = cols + 1;
    int icount = cols * rows * 6;
    unsigned short *idata =
        (unsigned short *)malloc((size_t)icount * sizeof(unsigned short));
    if (!idata)
        return -1;
    int t = 0;
    for (int j = 0; j < rows; j++) {
        for (int i = 0; i < cols; i++) {
            unsigned short a = (unsigned short)(j * vcols + i);
            unsigned short b = (unsigned short)(j * vcols + i + 1);
            unsigned short c = (unsigned short)((j + 1) * vcols + i);
            unsigned short d = (unsigned short)((j + 1) * vcols + i + 1);
            idata[t++] = a; idata[t++] = c; idata[t++] = b;
            idata[t++] = b; idata[t++] = c; idata[t++] = d;
        }
    }

    const int stride = 6; /* xyz, uv, turb */
    FlowQuad *q = &g_flow.quads[g_flow.count];
    glGenVertexArrays(1, &q->vao);
    glBindVertexArray(q->vao);
    glGenBuffers(1, &q->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, q->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)(vcount * stride * sizeof(float)), vdata,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)(5 * sizeof(float)));
    glGenBuffers(1, &q->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, q->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)(icount * sizeof(unsigned short)),
                 idata, GL_STATIC_DRAW);
    glBindVertexArray(0);

    q->index_count = icount;
    q->speed = speed;
    q->tiling = tiling;
    q->active = 1;
    free(idata);
    return g_flow.count++;
}
#endif

int g3d_flow_add(float tx, float ty, float tz, float bx, float by, float bz,
                 float width, float speed, float tiling) {
    flow_lazy_init();
    if (!g_flow.shader)
        return -1;
#ifndef VITA
    int cols = FLOW_COLS, rows = FLOW_ROWS;
    int vcols = cols + 1, vrows = rows + 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;

    /* Free-falling sheet: turbulence from how vertical the drop is */
    float dropy = fabsf(ty - by);
    float horiz = sqrtf((bx - tx) * (bx - tx) + (bz - tz) * (bz - tz)) + 0.001f;
    float turb = dropy / (dropy + horiz);     /* 0 flat .. 1 vertical */
    turb = 0.35f + 0.65f * turb;

    float hw = width * 0.5f;
    int k = 0;
    for (int j = 0; j < vrows; j++) {
        float fv = (float)j / (float)rows;
        float cx = tx + (bx - tx) * fv;
        float cy = ty + (by - ty) * fv;
        float cz = tz + (bz - tz) * fv;
        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            vdata[k++] = cx + ox;
            vdata[k++] = cy;
            vdata[k++] = cz;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    return -1;
#endif
}

int g3d_flow_add_river(void *terrain_mesh, float x0, float z0, float x1,
                       float z1, float width, float y_offset, float speed,
                       float tiling) {
    flow_lazy_init();
    if (!g_flow.shader || !terrain_mesh)
        return -1;
#ifndef VITA
    G3DMesh *terrain = (G3DMesh *)terrain_mesh;
    int cols = FLOW_COLS, rows = FLOW_ROWS * 2; /* more rows: follow contours */
    int vcols = cols + 1, vrows = rows + 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;

    /* Direction along the river (XZ) and the perpendicular for the width */
    float dx = x1 - x0, dz = z1 - z0;
    float dlen = sqrtf(dx * dx + dz * dz);
    if (dlen < 1e-4f) dlen = 1e-4f;
    dx /= dlen; dz /= dlen;
    float px = -dz, pz = dx; /* perpendicular in XZ */
    float hw = width * 0.5f;
    float seg = dlen / (float)rows; /* world length between rows */

    int k = 0;
    for (int j = 0; j < vrows; j++) {
        float fv = (float)j / (float)rows;
        float cx = x0 + (x1 - x0) * fv;
        float cz = z0 + (z1 - z0) * fv;

        /* Local slope along the flow at this row -> turbulence (steep = foam) */
        float fa = (j > 0) ? (float)(j - 1) / (float)rows : 0.0f;
        float fb = (j < rows) ? (float)(j + 1) / (float)rows : 1.0f;
        float ya = g3d_terrain_get_height(terrain, x0 + (x1 - x0) * fa,
                                          z0 + (z1 - z0) * fa);
        float yb = g3d_terrain_get_height(terrain, x0 + (x1 - x0) * fb,
                                          z0 + (z1 - z0) * fb);
        float slope = fabsf(yb - ya) / (2.0f * seg + 1e-4f);
        float turb = slope / (slope + 0.6f); /* 0 flat .. ->1 steep */

        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            float wx = cx + px * ox;
            float wz = cz + pz * ox;
            /* Conform to the terrain surface: sample its height + offset */
            float wy = g3d_terrain_get_height(terrain, wx, wz) + y_offset;
            vdata[k++] = wx;
            vdata[k++] = wy;
            vdata[k++] = wz;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    return -1;
#endif
}

int g3d_flow_add_path(const float *pts, int n, float width, float y_offset,
                      float speed, float tiling) {
    flow_lazy_init();
    if (!g_flow.shader || !pts || n < 2)
        return -1;
#ifndef VITA
    int cols = FLOW_COLS;
    int vcols = cols + 1, vrows = n, rows = n - 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;
    float hw = width * 0.5f;

    /* total length for normalized UV.v along the river */
    float total = 0.0f;
    for (int j = 0; j < n - 1; j++) {
        float ddx = pts[(j + 1) * 3] - pts[j * 3];
        float ddz = pts[(j + 1) * 3 + 2] - pts[j * 3 + 2];
        total += sqrtf(ddx * ddx + ddz * ddz);
    }
    if (total < 1e-4f) total = 1e-4f;

    float cum = 0.0f;
    int k = 0;
    for (int j = 0; j < n; j++) {
        float cx = pts[j * 3], cy = pts[j * 3 + 1], cz = pts[j * 3 + 2];
        /* flow direction = average of the adjacent segments (smooth bends) */
        float dx = 0.0f, dz = 0.0f;
        if (j > 0)     { dx += cx - pts[(j - 1) * 3]; dz += cz - pts[(j - 1) * 3 + 2]; }
        if (j < n - 1) { dx += pts[(j + 1) * 3] - cx; dz += pts[(j + 1) * 3 + 2] - cz; }
        float dl = sqrtf(dx * dx + dz * dz); if (dl < 1e-4f) dl = 1e-4f;
        dx /= dl; dz /= dl;
        float px = -dz, pz = dx;  /* perpendicular in XZ */

        float turb = 0.2f;
        if (j > 0) {
            float dy = fabsf(cy - pts[(j - 1) * 3 + 1]);
            float sx = cx - pts[(j - 1) * 3], sz = cz - pts[(j - 1) * 3 + 2];
            float seg = sqrtf(sx * sx + sz * sz) + 1e-4f;
            float slope = dy / seg;
            turb = slope / (slope + 0.6f);
            cum += seg;
        }
        float fv = cum / total;

        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            vdata[k++] = cx + px * ox;
            vdata[k++] = cy + y_offset;
            vdata[k++] = cz + pz * ox;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    (void)pts; (void)n; (void)width; (void)y_offset; (void)speed; (void)tiling;
    return -1;
#endif
}

void g3d_river_add_waterfalls(const float *pts, int n, const float *H,
                              int side, float ws, float width) {
    if (!pts || n < 2 || !H) return;
    for (int i = 0; i < n - 1; i++) {
        float tx = pts[i * 3], tz = pts[i * 3 + 2];
        float bx = pts[(i + 1) * 3], bz = pts[(i + 1) * 3 + 2];
        float ty = g3d_heightfield_height(H, side, ws, tx, tz);
        float by = g3d_heightfield_height(H, side, ws, bx, bz);
        float drop = ty - by;
        float dx = bx - tx, dz = bz - tz;
        float horiz = sqrtf(dx * dx + dz * dz) + 1e-4f;
        /* steep enough to read as a fall (slope > ~45 deg and > 2 units) */
        if (drop > 2.0f && drop > horiz) {
            float tiling = drop * 0.4f; if (tiling < 1.0f) tiling = 1.0f;
            g3d_flow_add(tx, ty + 0.3f, tz, bx, by - 0.3f, bz, width, 2.5f, tiling);
        }
    }
}

void g3d_flow_render_pass(G3DCamera *camera, int flip_y) {
    if (!g_flow.initialized || !g_flow.shader || g_flow.count == 0 || !camera)
        return;

#ifndef VITA
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }
    float time = (float)SDL_GetTicks() / 1000.0f;

    g3d_shader_use(g_flow.shader);
    g3d_shader_set_mat4(g_flow.shader, "uView", view);
    g3d_shader_set_mat4(g_flow.shader, "uProjection", proj);
    g3d_shader_set_float(g_flow.shader, "uTime", time);
    g3d_shader_set_vec3(g_flow.shader, "uColor",
                        vec3_make(g_flow.color[0], g_flow.color[1], g_flow.color[2]));
    g3d_shader_set_int(g_flow.shader, "uClipOn", g_flow.clip_on);
    g3d_shader_set_float(g_flow.shader, "uClipY", g_flow.clip_y);
    g3d_shader_set_vec3(g_flow.shader, "uCameraPos", camera->position);

    /* Sun direction / colour from the active scene's directional light */
    Vec3 ldir = vec3_make(-0.5f, -1.0f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 1.0f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction;
            lcol = vec3_make(l->color[0], l->color[1], l->color[2]);
            break;
        }
    }
    g3d_shader_set_vec3(g_flow.shader, "uLightDir", ldir);
    g3d_shader_set_vec3(g_flow.shader, "uLightColor", lcol);

    if (g_flow.tex_handle) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_flow.tex_handle);
        g3d_shader_set_int(g_flow.shader, "uTex", 0);
        g3d_shader_set_int(g_flow.shader, "uHasTex", 1);
    } else {
        g3d_shader_set_int(g_flow.shader, "uHasTex", 0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    for (int i = 0; i < g_flow.count; i++) {
        FlowQuad *q = &g_flow.quads[i];
        if (!q->active)
            continue;
        g3d_shader_set_float(g_flow.shader, "uSpeed", q->speed);
        g3d_shader_set_float(g_flow.shader, "uTiling", q->tiling);
        glBindVertexArray(q->vao);
        glDrawElements(GL_TRIANGLES, q->index_count, GL_UNSIGNED_SHORT, 0);
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

void g3d_flow_clear(void) {
#ifndef VITA
    for (int i = 0; i < g_flow.count; i++) {
        FlowQuad *q = &g_flow.quads[i];
        if (q->vao) glDeleteVertexArrays(1, &q->vao);
        if (q->vbo) glDeleteBuffers(1, &q->vbo);
        if (q->ebo) glDeleteBuffers(1, &q->ebo);
        q->active = 0;
        q->vao = q->vbo = q->ebo = 0;
    }
#endif
    g_flow.count = 0;
}

void g3d_flow_shutdown(void) {
    g3d_flow_clear();
    if (g_flow.shader) {
        g3d_shader_free(g_flow.shader);
        g_flow.shader = NULL;
    }
    g_flow.initialized = 0;
}
