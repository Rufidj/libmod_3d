/*
 * libmod_3d_mirror.c - Flat planar mirrors (pool, each with own reflection)
 */

#include "libmod_3d_mirror.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <math.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define MAX_MIRRORS 32

static const char *mirror_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "out vec4 vClip;\n"
    "void main() {\n"
    "    gl_Position = uProjection * uView * vec4(position, 1.0);\n"
    "    vClip = gl_Position;\n"
    "}\n";

static const char *mirror_frag =
    "#version 330 core\n"
    "in vec4 vClip;\n"
    "uniform sampler2D uReflTex;\n"
    "uniform vec3 uTint;\n"
    "uniform int uFlipV;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec2 uv = vClip.xy / vClip.w * 0.5 + 0.5;\n"
    "    if (uFlipV == 1) uv.y = 1.0 - uv.y;\n"
    "    vec3 col = texture(uReflTex, clamp(uv, 0.0, 1.0)).rgb * uTint;\n"
    "    FragColor = vec4(col, 1.0);\n"
    "}\n";

typedef struct {
    int active;
    float p[3];
    float n[3];
    float tint[3];
    int flip;
    unsigned int vao, vbo;
    unsigned int fbo, tex, depth;
    int w, h;
    Vec3 aabb_min, aabb_max;   /* quad bounds, for frustum culling */
} Mirror;

static struct {
    int initialized;
    G3DShaderProgram *shader;
    Mirror m[MAX_MIRRORS];
    int count;
    float max_dist;
} g_mir = {0};

void g3d_mirror_set_max_distance(float dist) {
    g_mir.max_dist = (dist > 1.0f) ? dist : 1.0f;
}

static int mirror_init(void) {
    if (g_mir.initialized)
        return 1;
#ifndef VITA
    g_mir.shader = g3d_shader_create(mirror_vert, mirror_frag);
    if (!g_mir.shader) {
        fprintf(stderr, "G3D: mirror shader failed\n");
        return 0;
    }
#endif
    if (g_mir.max_dist <= 0.0f) g_mir.max_dist = 80.0f;
    g_mir.initialized = 1;
    return 1;
}

#ifndef VITA
static void make_fbo(Mirror *mr, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    mr->w = w; mr->h = h;
    glGenFramebuffers(1, &mr->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mr->fbo);
    glGenTextures(1, &mr->tex);
    glBindTexture(GL_TEXTURE_2D, mr->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mr->tex, 0);
    glGenRenderbuffers(1, &mr->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, mr->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mr->depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "G3D: mirror framebuffer incomplete\n");
    /* clear to sky so a not-yet-rendered (far) mirror isn't garbage */
    glClearColor(0.62f, 0.74f, 0.90f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
#endif

int g3d_mirror_create(float px, float py, float pz,
                      float nx, float ny, float nz,
                      float width, float height) {
    if (!mirror_init())
        return -1;
    int idx = -1;
    for (int i = 0; i < MAX_MIRRORS; i++)
        if (!g_mir.m[i].active) { idx = i; break; }
    if (idx < 0)
        return -1;
    Mirror *mr = &g_mir.m[idx];

    float nl = sqrtf(nx*nx + ny*ny + nz*nz);
    if (nl < 1e-6f) nl = 1.0f;
    nx /= nl; ny /= nl; nz /= nl;
    mr->p[0] = px; mr->p[1] = py; mr->p[2] = pz;
    mr->n[0] = nx; mr->n[1] = ny; mr->n[2] = nz;
    mr->tint[0] = mr->tint[1] = mr->tint[2] = 0.92f;
    mr->flip = 0;

    Vec3 N = vec3_make(nx, ny, nz);
    Vec3 up = (fabsf(ny) > 0.99f) ? vec3_make(0, 0, 1) : vec3_make(0, 1, 0);
    Vec3 right = vec3_normalize(vec3_cross(up, N));
    Vec3 u = vec3_normalize(vec3_cross(N, right));
    float hw = width * 0.5f, hh = height * 0.5f;
    Vec3 P = vec3_make(px, py, pz);
    Vec3 c00 = vec3_add(P, vec3_add(vec3_scale(right, -hw), vec3_scale(u, -hh)));
    Vec3 c10 = vec3_add(P, vec3_add(vec3_scale(right,  hw), vec3_scale(u, -hh)));
    Vec3 c11 = vec3_add(P, vec3_add(vec3_scale(right,  hw), vec3_scale(u,  hh)));
    Vec3 c01 = vec3_add(P, vec3_add(vec3_scale(right, -hw), vec3_scale(u,  hh)));
    float verts[18] = {
        c00.x, c00.y, c00.z,  c10.x, c10.y, c10.z,  c11.x, c11.y, c11.z,
        c00.x, c00.y, c00.z,  c11.x, c11.y, c11.z,  c01.x, c01.y, c01.z
    };
    /* quad AABB for frustum culling */
    mr->aabb_min = c00; mr->aabb_max = c00;
    Vec3 cs[3] = {c10, c11, c01};
    for (int k = 0; k < 3; k++) {
        if (cs[k].x < mr->aabb_min.x) mr->aabb_min.x = cs[k].x;
        if (cs[k].y < mr->aabb_min.y) mr->aabb_min.y = cs[k].y;
        if (cs[k].z < mr->aabb_min.z) mr->aabb_min.z = cs[k].z;
        if (cs[k].x > mr->aabb_max.x) mr->aabb_max.x = cs[k].x;
        if (cs[k].y > mr->aabb_max.y) mr->aabb_max.y = cs[k].y;
        if (cs[k].z > mr->aabb_max.z) mr->aabb_max.z = cs[k].z;
    }
#ifndef VITA
    glGenVertexArrays(1, &mr->vao);
    glGenBuffers(1, &mr->vbo);
    glBindVertexArray(mr->vao);
    glBindBuffer(GL_ARRAY_BUFFER, mr->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glBindVertexArray(0);

    /* Reflection at half display resolution (cheap, barely noticeable) */
    uint32_t dw = 1280, dh = 720;
    g3d_renderer_get_display_size(&dw, &dh);
    make_fbo(mr, (int)dw / 2, (int)dh / 2);
#endif

    mr->active = 1;
    if (idx + 1 > g_mir.count) g_mir.count = idx + 1;
    return idx;
}

void g3d_mirror_set_tint(int index, float r, float g, float b) {
    if (index < 0 || index >= MAX_MIRRORS || !g_mir.m[index].active) return;
    g_mir.m[index].tint[0] = r; g_mir.m[index].tint[1] = g; g_mir.m[index].tint[2] = b;
}

void g3d_mirror_set_flip(int index, int flip) {
    if (index < 0 || index >= MAX_MIRRORS || !g_mir.m[index].active) return;
    g_mir.m[index].flip = flip ? 1 : 0;
}

void g3d_mirror_clear(void) {
#ifndef VITA
    for (int i = 0; i < MAX_MIRRORS; i++) {
        Mirror *mr = &g_mir.m[i];
        if (!mr->active) continue;
        if (mr->vbo) glDeleteBuffers(1, &mr->vbo);
        if (mr->vao) glDeleteVertexArrays(1, &mr->vao);
        if (mr->tex) glDeleteTextures(1, &mr->tex);
        if (mr->depth) glDeleteRenderbuffers(1, &mr->depth);
        if (mr->fbo) glDeleteFramebuffers(1, &mr->fbo);
        mr->active = 0;
    }
#endif
    g_mir.count = 0;
}

void g3d_mirror_render_reflections(G3DCamera *camera) {
    if (!g_mir.initialized || !camera) return;
    g3d_camera_update(camera);
    g3d_camera_update_frustum(camera);
    Vec3 cp = camera->position;
    float maxd2 = g_mir.max_dist * g_mir.max_dist;

    for (int i = 0; i < MAX_MIRRORS; i++) {
        Mirror *mr = &g_mir.m[i];
        if (!mr->active) continue;

        /* facing: only reflect if the camera is in front of the mirror */
        float side = (cp.x - mr->p[0]) * mr->n[0] +
                     (cp.y - mr->p[1]) * mr->n[1] +
                     (cp.z - mr->p[2]) * mr->n[2];
        if (side <= 0.05f) continue;

        /* distance: skip far mirrors (they keep last frame's reflection) */
        float dx = cp.x - mr->p[0], dy = cp.y - mr->p[1], dz = cp.z - mr->p[2];
        if (dx*dx + dy*dy + dz*dz > maxd2) continue;

        /* frustum: skip mirrors not on screen */
        if (!g3d_camera_frustum_contains_aabb(camera, mr->aabb_min, mr->aabb_max))
            continue;

        g3d_renderer_reflection_pass_plane(mr->p[0], mr->p[1], mr->p[2],
                                           mr->n[0], mr->n[1], mr->n[2],
                                           mr->fbo, mr->w, mr->h);
    }
}

void g3d_mirror_render_surfaces(G3DCamera *camera, int flip_y) {
    if (!g_mir.initialized || !g_mir.shader || !camera) return;
#ifndef VITA
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }
    g3d_shader_use(g_mir.shader);
    g3d_shader_set_mat4(g_mir.shader, "uView", view);
    g3d_shader_set_mat4(g_mir.shader, "uProjection", proj);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < MAX_MIRRORS; i++) {
        Mirror *mr = &g_mir.m[i];
        if (!mr->active) continue;
        glBindTexture(GL_TEXTURE_2D, mr->tex);
        g3d_shader_set_int(g_mir.shader, "uReflTex", 0);
        g3d_shader_set_vec3(g_mir.shader, "uTint",
                            vec3_make(mr->tint[0], mr->tint[1], mr->tint[2]));
        g3d_shader_set_int(g_mir.shader, "uFlipV", mr->flip);
        glBindVertexArray(mr->vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
#endif
}

void g3d_mirror_shutdown(void) {
    g3d_mirror_clear();
#ifndef VITA
    if (g_mir.shader) g3d_shader_free(g_mir.shader);
#endif
    g_mir.shader = NULL;
    g_mir.initialized = 0;
}
