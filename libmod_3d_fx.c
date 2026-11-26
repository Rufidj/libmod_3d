/*
 * libmod_3d_fx.c - humo, llamas y chispas (ver libmod_3d_fx.h).
 */
#include "libmod_3d_fx.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define FX_MAX 6144
typedef struct { float x, y, z, vx, vy, vz, rx, ry, rz, age, life, s0, s1, alpha, stretch, seed; int kind, pal, alive; } FxP;
static FxP g_fx[FX_MAX];
static int g_fx_head = 0;
static unsigned int g_fx_seed = 987654321u;
static float fxr(void) { g_fx_seed = g_fx_seed * 1103515245u + 12345u; return (float)((g_fx_seed >> 8) & 0xFFFF) / 65535.0f; }

void g3d_fx_emit(int kind, int pal, float x, float y, float z, float vx, float vy, float vz,
                 float life, float size0, float size1, float alpha, float stretch, float rvx, float rvy, float rvz) {
    FxP *p = &g_fx[g_fx_head]; g_fx_head = (g_fx_head + 1) % FX_MAX;
    p->x = x; p->y = y; p->z = z; p->vx = vx; p->vy = vy; p->vz = vz;
    p->age = 0; p->life = life > 0.02f ? life : 0.02f; p->s0 = size0; p->s1 = size1; p->alpha = alpha; p->stretch = stretch;
    p->rx = rvx; p->ry = rvy; p->rz = rvz; p->seed = fxr(); p->kind = kind; p->pal = pal; p->alive = 1;
}
void g3d_fx_clear(void) { memset(g_fx, 0, sizeof(g_fx)); g_fx_head = 0; }

#ifndef VITA
#define VF 11   /* floats por vertice: pos3 uv2 p4 a2 */
static G3DShaderProgram *g_fx_shader = NULL;
static GLuint g_fx_vao = 0, g_fx_vbo = 0;
static float *g_fx_buf = NULL;

static const char *fx_vert =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec4 aP; layout(location=3) in vec2 aA;\n"
    "uniform mat4 uVP; out vec2 vUV; out vec4 vP; out vec2 vA;\n"
    "void main(){ vUV=aUV; vP=aP; vA=aA; gl_Position = uVP * vec4(aPos,1.0); }\n";

static const char *fx_frag =
    "#version 330 core\n"
    "in vec2 vUV; in vec4 vP; in vec2 vA; out vec4 F;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
    "float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }\n"
    "float fbm(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<4;i++){ s+=a*vn(p); p=p*2.03+vec2(1.7,9.2); a*=0.5; } return s; }\n"
    "void main(){\n"
    "  float age=vP.x, seed=vP.y; int kind=int(vP.z+0.5); int pal=int(vP.w+0.5);\n"
    "  float alpha=vA.x; float stretched=vA.y;\n"
    "  vec2 p = vUV*2.0-1.0;\n"
    "  if (kind==0) {\n"                                   /* ---- humo ---- */
    "    float r = length(p);\n"
    "    float n = fbm(p*1.7 + vec2(seed*17.0, seed*9.0 + age*1.2));\n"
    "    float a = smoothstep(1.0, 0.10, r + (n-0.5)*0.95);\n"
    "    vec3 base = (pal==1) ? vec3(0.12,0.115,0.12) : vec3(0.70,0.70,0.72);\n"
    "    vec3 col = base * (0.78 + 0.34*n) * (1.0 - 0.18*smoothstep(-0.6,0.9,p.y));\n"   /* mas oscuro abajo */
    "    F = vec4(col, a*alpha);\n"
    "  } else if (kind==1) {\n"                            /* ---- llama ---- */
    "    float body;\n"
    "    float n = fbm(p*2.2 + vec2(seed*23.0 - age*3.5, seed*5.0));\n"
    "    if (stretched > 0.5) {\n"                         /* lengua: ancha en el origen (x=0), fina en la punta (x=1) */
    "      float t = vUV.x; float w = pow(max(1.0-t,0.0),0.7)*0.95 + 0.04;\n"
    "      float dd = abs(p.y)/w;\n"
    "      body = smoothstep(1.0, 0.25, dd + (n-0.5)*0.7) * smoothstep(0.0,0.07,t) * smoothstep(1.0,0.55,t);\n"
    "    } else {\n"
    "      body = smoothstep(1.0, 0.2, length(p) + (n-0.5)*0.8);\n"
    "    }\n"
    "    float T = clamp(body * (1.05 - age*0.9) * (0.75 + 0.5*n), 0.0, 1.0);\n"
    "    vec3 c0, c1, c2;\n"
    "    if (pal==1) { c0=vec3(0.05,0.18,1.0); c1=vec3(0.15,0.65,1.0); c2=vec3(0.85,0.95,1.0); }\n"
    "    else if (pal==2) { c0=vec3(0.45,0.05,0.9); c1=vec3(0.85,0.30,1.0); c2=vec3(1.0,0.88,1.0); }\n"
    "    else { c0=vec3(1.0,0.10,0.02); c1=vec3(1.0,0.62,0.05); c2=vec3(1.0,0.96,0.72); }\n"
    "    vec3 col = mix(c0, c1, smoothstep(0.12,0.50,T));\n"
    "    col = mix(col, c2, smoothstep(0.62,1.0,T));\n"
    "    float a = clamp(T*1.5,0.0,1.0) * alpha;\n"
    "    F = vec4(col*a*1.25, a*0.50);\n"                  /* ONE, ONE_MINUS_SRC_ALPHA: brilla pero no se quema en blanco */
    "  } else {\n"                                         /* ---- chispa ---- */
    "    float r = (stretched > 0.5) ? length(vec2(p.x*0.9, p.y*2.2)) : length(p);\n"
    "    float core = clamp(1.0 - r, 0.0, 1.0);\n"
    "    vec3 c;\n"
    "    if (pal==1) c=vec3(0.30,0.60,1.0); else if (pal==2) c=vec3(1.0,0.55,0.10); else if (pal==3) c=vec3(0.85,0.35,1.0); else c=vec3(1.0,0.92,0.55);\n"
    "    vec3 col = mix(c, vec3(1.0), core*core*0.7);\n"
    "    float a = core*core * alpha * (1.0-age*0.6);\n"
    "    F = vec4(col*a*1.4, a*0.55);\n"
    "  }\n"
    "}\n";

static void ensure_fx_gl(void) {
    if (g_fx_shader) return;
    g_fx_shader = g3d_shader_create(fx_vert, fx_frag);
    g_fx_buf = (float *)malloc(sizeof(float) * VF * 6 * FX_MAX);
    glGenVertexArrays(1, &g_fx_vao); glGenBuffers(1, &g_fx_vbo);
    glBindVertexArray(g_fx_vao); glBindBuffer(GL_ARRAY_BUFFER, g_fx_vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VF * 4, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, VF * 4, (void *)(3 * 4));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, VF * 4, (void *)(5 * 4));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, VF * 4, (void *)(9 * 4));
    glBindVertexArray(0);
}

static float *g_depth_ref;
static int cmp_depth(const void *a, const void *b) {   /* de lejos a cerca */
    float da = g_depth_ref[*(const int *)a], db = g_depth_ref[*(const int *)b];
    return (da < db) - (da > db);
}

/* Escribe el quad de una particula y devuelve los floats escritos. */
static int emit_quad(float *o, const FxP *p, float size, float alpha, Vec3 cR, Vec3 cU) {
    float u = p->age / p->life;
    Vec3 A = cR, B = cU; float la = size, lb = size; float st = 0.0f;
    if ((p->kind == 1 && p->stretch > 0.01f) || (p->kind == 2 && p->stretch > 0.01f)) {
        float sp = sqrtf(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
        if (sp > 0.05f) {
            Vec3 v = vec3_make(p->vx / sp, p->vy / sp, p->vz / sp);
            Vec3 f = vec3_cross(cR, cU);                      /* hacia donde mira la camara (o su inverso, da igual) */
            Vec3 w = vec3_cross(v, f); float wl = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
            if (wl > 0.02f) {
                A = v; B = vec3_make(w.x / wl, w.y / wl, w.z / wl);
                la = size * (1.0f + p->stretch); lb = size; st = 1.0f;
            }
        }
    }
    if (st < 0.5f && p->kind == 0) {                      /* el humo gira despacio */
        float ang = p->seed * 6.2832f + u * (p->seed - 0.5f) * 3.0f, c = cosf(ang), s = sinf(ang);
        A = vec3_make(cR.x * c + cU.x * s, cR.y * c + cU.y * s, cR.z * c + cU.z * s);
        B = vec3_make(-cR.x * s + cU.x * c, -cR.y * s + cU.y * c, -cR.z * s + cU.z * c);
    }
    static const float cx[6] = { -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f };
    static const float cy[6] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f };
    for (int k = 0; k < 6; k++) {
        float ox = cx[k], oy = cy[k];
        /* en estirado el centro del quad se adelanta media longitud: el origen de la llama queda en x=0 */
        float px = p->x + A.x * ox * la + B.x * oy * lb + (st > 0.5f ? A.x * la * 0.5f : 0.0f);
        float py = p->y + A.y * ox * la + B.y * oy * lb + (st > 0.5f ? A.y * la * 0.5f : 0.0f);
        float pz = p->z + A.z * ox * la + B.z * oy * lb + (st > 0.5f ? A.z * la * 0.5f : 0.0f);
        o[0] = px; o[1] = py; o[2] = pz; o[3] = ox + 0.5f; o[4] = oy + 0.5f;
        o[5] = u; o[6] = p->seed; o[7] = (float)p->kind; o[8] = (float)p->pal; o[9] = alpha; o[10] = st;
        o += VF;
    }
    return VF * 6;
}
#endif

void g3d_fx_render(G3DCamera *camera, int flip_y) {
#ifndef VITA
    if (!camera) return;
    static Uint32 last = 0; Uint32 now = SDL_GetTicks();
    float dt = last ? (now - last) / 1000.0f : 0.016f; last = now;
    if (dt > 0.05f) dt = 0.05f;

    int live = 0;
    for (int i = 0; i < FX_MAX; i++) {
        FxP *p = &g_fx[i]; if (!p->alive) continue;
        p->age += dt; if (p->age >= p->life) { p->alive = 0; continue; }
        live++;
        if (p->kind == 0)      { float d = 1.0f - 1.3f * dt; p->vx *= d; p->vy *= d; p->vz *= d; p->vy += 0.5f * dt; }
        else if (p->kind == 1) { float d = 1.0f - 2.2f * dt; p->vx *= d; p->vy *= d; p->vz *= d; p->vy += 1.6f * dt; }
        else                   { p->vy -= 9.8f * dt; }
        p->x += (p->vx + p->rx) * dt; p->y += (p->vy + p->ry) * dt; p->z += (p->vz + p->rz) * dt;
    }
    if (!live) return;

    ensure_fx_gl();
    if (!g_fx_shader) return;
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 vp = mat4_multiply(proj, view);
    Vec3 cR = g3d_camera_get_right(camera), cU = g3d_camera_get_up(camera), cF = g3d_camera_get_forward(camera);

    g3d_shader_use(g_fx_shader);
    g3d_shader_set_mat4(g_fx_shader, "uVP", vp);
    glEnable(GL_BLEND); glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glBindVertexArray(g_fx_vao); glBindBuffer(GL_ARRAY_BUFFER, g_fx_vbo);

    /* ---- humo: de lejos a cerca, mezcla normal ---- */
    static int idx[FX_MAX]; static float dep[FX_MAX]; int n = 0;
    for (int i = 0; i < FX_MAX; i++) if (g_fx[i].alive && g_fx[i].kind == 0) { dep[i] = g_fx[i].x * cF.x + g_fx[i].y * cF.y + g_fx[i].z * cF.z; idx[n++] = i; }
    if (n) {
        g_depth_ref = dep; qsort(idx, n, sizeof(int), cmp_depth);
        float *o = g_fx_buf;
        for (int k = 0; k < n; k++) {
            FxP *p = &g_fx[idx[k]]; float u = p->age / p->life;
            float env = (u < 0.12f ? u / 0.12f : 1.0f - (u - 0.12f) / 0.88f);
            o += emit_quad(o, p, p->s0 + (p->s1 - p->s0) * u, p->alpha * env, cR, cU);
        }
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((char *)o - (char *)g_fx_buf), g_fx_buf, GL_STREAM_DRAW);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, n * 6);
    }
    /* ---- llamas y chispas: mezcla "brillante" (premultiplicada, parte aditiva) ---- */
    n = 0; { float *o = g_fx_buf;
        for (int i = 0; i < FX_MAX; i++) {
            FxP *p = &g_fx[i]; if (!p->alive || p->kind == 0) continue;
            float u = p->age / p->life;
            float env = (u < 0.06f ? u / 0.06f : 1.0f);
            if (p->kind == 2) env = 1.0f;
            o += emit_quad(o, p, p->s0 + (p->s1 - p->s0) * u, p->alpha * env, cR, cU); n++;
        }
        if (n) {
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((char *)o - (char *)g_fx_buf), g_fx_buf, GL_STREAM_DRAW);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, 0, n * 6);
        }
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND);
#else
    (void)camera; (void)flip_y;
#endif
}
