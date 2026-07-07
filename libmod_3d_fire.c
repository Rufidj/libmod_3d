/*
 * libmod_3d_fire.c - Procedural shader fire + flickering torch light.
 */
#include "libmod_3d_fire.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_light.h"
#include "libmod_3d_math.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define FIRE_MAX 128

typedef struct { float x, y, z, size; int light; float seed; float base_int; int active;
                 float smoke_acc, spark_acc; } Fire;
static Fire g_fires[FIRE_MAX];
static int  g_nfire = 0;

/* smoke + spark particles (shared pool) */
typedef struct { float x, y, z, vx, vy, vz, age, life, s0, s1; int kind; int alive; } FPart; /* kind 0=smoke 1=spark */
#define FPART_MAX 4096
static FPart g_fp[FPART_MAX];
static int   g_fp_head = 0;
static unsigned int g_fp_seed = 12345;
static float frnd(void) { g_fp_seed = g_fp_seed * 1103515245u + 12345u; return (float)((g_fp_seed >> 8) & 0xFFFF) / 65535.0f; }
static void fp_spawn(int kind, float x, float y, float z, float vx, float vy, float vz, float life, float s0, float s1) {
    FPart *p = &g_fp[g_fp_head]; g_fp_head = (g_fp_head + 1) % FPART_MAX;
    p->x=x; p->y=y; p->z=z; p->vx=vx; p->vy=vy; p->vz=vz; p->age=0; p->life=life; p->s0=s0; p->s1=s1; p->kind=kind; p->alive=1;
}

#ifndef VITA
static G3DShaderProgram *g_fire_shader = NULL;
static GLuint g_fire_vao = 0, g_fire_vbo = 0;
static G3DShaderProgram *g_part_shader = NULL;
static GLuint g_part_vao = 0, g_part_vbo = 0;

static const char *part_vert =
    "#version 330 core\n"
    "layout(location=0) in vec2 aCorner;\n"       /* centred quad [-0.5,0.5]^2 */
    "uniform vec3 uCenter; uniform vec3 uRight; uniform vec3 uUp;\n"
    "uniform float uSize; uniform mat4 uViewProj;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV = aCorner + 0.5;\n"
    "  vec3 wp = uCenter + uRight*(aCorner.x*uSize) + uUp*(aCorner.y*uSize);\n"
    "  gl_Position = uViewProj * vec4(wp, 1.0); }\n";
static const char *part_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F; uniform vec3 uColor; uniform float uAlpha; uniform float uSoft;\n"
    "void main(){ float d = length(vUV-0.5)*2.0; float a = smoothstep(1.0, uSoft, d); F = vec4(uColor, a*uAlpha); }\n";

static const char *fire_vert =
    "#version 330 core\n"
    "layout(location=0) in vec2 aCorner;\n"       /* x in [-0.5,0.5], y in [0,1] */
    "uniform vec3 uCenter; uniform vec3 uRight; uniform vec3 uUp;\n"
    "uniform float uW; uniform float uH; uniform mat4 uViewProj;\n"
    "out vec2 vUV;\n"
    "void main(){\n"
    "  vUV = vec2(aCorner.x + 0.5, aCorner.y);\n"
    "  vec3 wp = uCenter + uRight * (aCorner.x * uW) + uUp * (aCorner.y * uH);\n"
    "  gl_Position = uViewProj * vec4(wp, 1.0);\n"
    "}\n";

static const char *fire_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform float uTime; uniform float uSeed;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
    "float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }\n"
    "float fbm(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<5;i++){ s+=a*vn(p); p=p*2.02; a*=0.5; } return s; }\n"
    "void main(){\n"
    "  vec2 uv = vUV; float cx = uv.x - 0.5; float t = uTime + uSeed * 10.0;\n"
    "  float n = fbm(vec2(uv.x*3.0 + uSeed*7.0, uv.y*4.0 - t*3.2));\n"        /* rising turbulence */
    "  float sway = (fbm(vec2(uv.y*2.0 - t*2.0, uSeed*3.0)) - 0.5) * 0.30 * uv.y;\n"
    "  float d = abs(cx - sway);\n"
    "  float width = mix(0.42, 0.015, uv.y) * (0.7 + 0.6*n);\n"              /* narrows toward the top */
    "  float body = smoothstep(width, width*0.3, d);\n"
    "  float vfall = smoothstep(1.05, 0.30, uv.y) * smoothstep(0.0, 0.12, uv.y);\n"
    "  float flame = clamp(body * vfall * (0.7 + 0.7*n), 0.0, 1.0);\n"
    /* colour ramp: red/orange -> yellow -> white-hot core (HDR > 1 so it blooms) */
    "  vec3 col = vec3(1.5, 0.20, 0.03) * smoothstep(0.05, 0.45, flame);\n"
    "  col += vec3(1.7, 1.10, 0.15) * smoothstep(0.35, 0.78, flame);\n"
    "  col += vec3(2.4, 2.1, 1.4) * smoothstep(0.72, 1.0, flame);\n"
    "  F = vec4(col, flame);\n"
    "}\n";

static void ensure_fire_gl(void) {
    if (g_fire_shader) return;
    g_fire_shader = g3d_shader_create(fire_vert, fire_frag);
    float quad[8] = { -0.5f,0.0f,  0.5f,0.0f,  -0.5f,1.0f,  0.5f,1.0f };
    glGenVertexArrays(1, &g_fire_vao);
    glGenBuffers(1, &g_fire_vbo);
    glBindVertexArray(g_fire_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_fire_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);

    /* centred quad for smoke/spark particles */
    g_part_shader = g3d_shader_create(part_vert, part_frag);
    float cq[8] = { -0.5f,-0.5f,  0.5f,-0.5f,  -0.5f,0.5f,  0.5f,0.5f };
    glGenVertexArrays(1, &g_part_vao);
    glGenBuffers(1, &g_part_vbo);
    glBindVertexArray(g_part_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_part_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cq), cq, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);
}
#endif

int g3d_fire_add(float x, float y, float z, float size) {
    if (g_nfire >= FIRE_MAX) return -1;
    Fire *f = &g_fires[g_nfire];
    f->x = x; f->y = y; f->z = z; f->size = size > 0.1f ? size : 1.0f;
    f->seed = (float)rand() / (float)RAND_MAX;
    f->base_int = 1.6f + size * 0.25f;
    f->active = 1;
    /* warm flickering point light at the flame's heart */
    f->light = g3d_light_impl_create(1 /* point */, 1.0f, 0.55f, 0.22f);
    if (f->light >= 0) {
        g3d_light_impl_set_position(f->light, x, y + f->size * 0.5f, z);
        g3d_light_impl_set_range(f->light, f->size * 14.0f);
        g3d_light_impl_set_intensity(f->light, f->base_int);
    }
    return g_nfire++;
}

void g3d_fire_move(int id, float x, float y, float z) {
    if (id < 0 || id >= g_nfire) return;
    g_fires[id].x = x; g_fires[id].y = y; g_fires[id].z = z;
}

void g3d_fire_clear(void) { g_nfire = 0; }
int  g3d_fire_count(void) { return g_nfire; }

void g3d_fire_render(G3DCamera *camera, int flip_y) {
#ifndef VITA
    if (!camera || g_nfire == 0) return;
    ensure_fire_gl();
    if (!g_fire_shader) return;

    float t = (float)SDL_GetTicks() / 1000.0f;

    /* flicker the torch lights */
    for (int i = 0; i < g_nfire; i++) {
        Fire *f = &g_fires[i];
        if (!f->active || f->light < 0) continue;
        float s = f->seed * 6.28f;
        float fl = 0.80f + 0.14f * sinf(t * 11.0f + s) + 0.08f * sinf(t * 23.0f + s * 2.0f)
                        + 0.05f * sinf(t * 41.0f + s * 3.7f);
        g3d_light_impl_set_intensity(f->light, f->base_int * fl);
        g3d_light_impl_set_position(f->light, f->x + 0.05f * sinf(t * 9.0f + s),
                                    f->y + f->size * 0.5f, f->z + 0.05f * cosf(t * 7.0f + s));
    }

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 vp = mat4_multiply(proj, view);

    Vec3 fwd = g3d_camera_get_forward(camera);
    Vec3 up = vec3_make(0.0f, 1.0f, 0.0f);
    /* cylindrical billboard: horizontal right, vertical up (flames stand upright) */
    Vec3 right = vec3_normalize(vec3_cross(up, fwd));

    g3d_shader_use(g_fire_shader);
    g3d_shader_set_mat4(g_fire_shader, "uViewProj", vp);
    g3d_shader_set_vec3(g_fire_shader, "uRight", right);
    g3d_shader_set_vec3(g_fire_shader, "uUp", up);
    g3d_shader_set_float(g_fire_shader, "uTime", t);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);      /* additive -> glowing fire */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(g_fire_vao);
    for (int i = 0; i < g_nfire; i++) {
        Fire *f = &g_fires[i];
        if (!f->active) continue;
        g3d_shader_set_vec3(g_fire_shader, "uCenter", vec3_make(f->x, f->y, f->z));
        g3d_shader_set_float(g_fire_shader, "uW", f->size * 0.9f);
        g3d_shader_set_float(g_fire_shader, "uH", f->size * 1.7f);
        g3d_shader_set_float(g_fire_shader, "uSeed", f->seed);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);

    /* ---- smoke + sparks ---- */
    static float last_t = 0.0f;
    float dt = (last_t > 0.0f) ? (t - last_t) : 0.016f; last_t = t;
    if (dt > 0.1f) dt = 0.1f; if (dt < 0.0f) dt = 0.0f;

    for (int i = 0; i < g_nfire; i++) {          /* emit from each fire */
        Fire *f = &g_fires[i]; if (!f->active) continue;
        float top = f->y + f->size * 1.3f;
        for (f->smoke_acc += dt; f->smoke_acc > 0.10f; f->smoke_acc -= 0.10f)
            fp_spawn(0, f->x + (frnd()-0.5f)*f->size*0.3f, top, f->z + (frnd()-0.5f)*f->size*0.3f,
                     (frnd()-0.5f)*0.3f, 1.2f + frnd()*0.6f, (frnd()-0.5f)*0.3f,
                     2.0f + frnd()*1.5f, f->size*0.5f, f->size*2.0f);
        for (f->spark_acc += dt; f->spark_acc > 0.05f; f->spark_acc -= 0.05f)
            if (frnd() < 0.6f)
                fp_spawn(1, f->x + (frnd()-0.5f)*f->size*0.2f, top - f->size*0.4f, f->z + (frnd()-0.5f)*f->size*0.2f,
                         (frnd()-0.5f)*1.5f, 2.5f + frnd()*2.5f, (frnd()-0.5f)*1.5f,
                         0.5f + frnd()*0.5f, f->size*0.06f, f->size*0.02f);
    }
    for (int i = 0; i < FPART_MAX; i++) {        /* update */
        FPart *p = &g_fp[i]; if (!p->alive) continue;
        p->age += dt; if (p->age >= p->life) { p->alive = 0; continue; }
        if (p->kind == 0) { p->vy *= (1.0f - 0.4f*dt); p->vx += (frnd()-0.5f)*0.3f*dt; p->vz += (frnd()-0.5f)*0.3f*dt; }
        else p->vy -= 9.0f * dt;
        p->x += p->vx*dt; p->y += p->vy*dt; p->z += p->vz*dt;
    }

    Vec3 cR = g3d_camera_get_right(camera), cU = g3d_camera_get_up(camera);
    g3d_shader_use(g_part_shader);
    g3d_shader_set_mat4(g_part_shader, "uViewProj", vp);
    g3d_shader_set_vec3(g_part_shader, "uRight", cR);
    g3d_shader_set_vec3(g_part_shader, "uUp", cU);
    glBindVertexArray(g_part_vao);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   /* smoke: soft grey */
    g3d_shader_set_float(g_part_shader, "uSoft", 0.0f);
    g3d_shader_set_vec3(g_part_shader, "uColor", vec3_make(0.18f, 0.18f, 0.20f));
    for (int i = 0; i < FPART_MAX; i++) {
        FPart *p = &g_fp[i]; if (!p->alive || p->kind != 0) continue;
        float u = p->age / p->life;
        float a = (u < 0.15f ? u/0.15f : 1.0f - (u-0.15f)/0.85f) * 0.22f;
        g3d_shader_set_vec3(g_part_shader, "uCenter", vec3_make(p->x, p->y, p->z));
        g3d_shader_set_float(g_part_shader, "uSize", p->s0 + (p->s1-p->s0)*u);
        g3d_shader_set_float(g_part_shader, "uAlpha", a);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);                   /* sparks: additive HDR */
    g3d_shader_set_float(g_part_shader, "uSoft", 0.35f);
    g3d_shader_set_vec3(g_part_shader, "uColor", vec3_make(2.4f, 1.1f, 0.25f));
    for (int i = 0; i < FPART_MAX; i++) {
        FPart *p = &g_fp[i]; if (!p->alive || p->kind != 1) continue;
        float u = p->age / p->life;
        g3d_shader_set_vec3(g_part_shader, "uCenter", vec3_make(p->x, p->y, p->z));
        g3d_shader_set_float(g_part_shader, "uSize", p->s0 + (p->s1-p->s0)*u);
        g3d_shader_set_float(g_part_shader, "uAlpha", 1.0f - u);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
#else
    (void)camera; (void)flip_y;
#endif
}
