/*
 * libmod_3d_weather.c - nieve y lluvia (ver libmod_3d_weather.h).
 */
#include "libmod_3d_weather.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include <SDL.h>
#include <math.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define WEATHER_MAX 9000

static int   g_w_type = 0;
static float g_w_int = 0.0f;
static float g_w_wind[2] = { 0.0f, 0.0f };
static float g_w_size = 1.0f, g_w_speed = 1.0f, g_w_col[3] = { 1.0f, 1.0f, 1.0f }, g_w_area = 50.0f;
static int   g_w_style_set = 0;

void g3d_weather_set(int type, float intensity) {
    g_w_type = type < 0 ? 0 : (type > 2 ? 2 : type);
    g_w_int = intensity < 0.0f ? 0.0f : (intensity > 1.0f ? 1.0f : intensity);
}
void g3d_weather_set_wind(float wx, float wz) { g_w_wind[0] = wx; g_w_wind[1] = wz; }
void g3d_weather_set_style(float size_mul, float speed_mul, float r, float g, float b, float area) {
    g_w_size = size_mul > 0.05f ? size_mul : 0.05f; g_w_speed = speed_mul > 0.05f ? speed_mul : 0.05f;
    g_w_col[0] = r; g_w_col[1] = g; g_w_col[2] = b; g_w_area = area > 10.0f ? area : 10.0f; g_w_style_set = 1;
}

#ifndef VITA
static G3DShaderProgram *g_w_shader = NULL;
static GLuint g_w_vao = 0;

static const char *w_vert =
    "#version 330 core\n"
    "uniform mat4 uVP; uniform vec3 uCam; uniform vec3 uRight; uniform float uTime; uniform float uCount;\n"
    "uniform vec3 uWind; uniform float uType; uniform float uSize; uniform float uSpeed; uniform vec3 uBox;\n"
    "out vec2 vUV; out float vA;\n"
    "float h1(float n){ return fract(sin(n*12.9898)*43758.5453); }\n"
    "void main(){\n"
    "  int id = gl_VertexID / 6; int c = gl_VertexID % 6; float fid = float(id);\n"
    "  if (fid >= uCount) { gl_Position = vec4(2.0,2.0,2.0,1.0); vUV = vec2(0.0); vA = 0.0; return; }\n"
    "  vec3 r = vec3(h1(fid+1.0), h1(fid+7.3), h1(fid+13.7));\n"
    "  bool snow = uType < 1.5;\n"
    "  float fall = snow ? (0.9 + 1.3*h1(fid+3.1)) : (16.0 + 7.0*h1(fid+3.1));\n"
    "  vec3 vel = vec3(uWind.x * (snow ? 1.0 : 0.35), -fall*uSpeed, uWind.z * (snow ? 1.0 : 0.35));\n"
    "  vec3 p = r*uBox + vel*uTime;\n"
    "  if (snow) { p.x += sin(uTime*(0.5+r.x*0.8)+fid)*0.7; p.z += cos(uTime*(0.4+r.z*0.8)+fid*1.7)*0.7; }\n"
    "  vec3 rel = mod(p - uCam + uBox*0.5, uBox) - uBox*0.5;\n"
    "  vec3 w = uCam + rel;\n"
    "  vec2 q = (c==0)?vec2(-0.5,-0.5):(c==1)?vec2(0.5,-0.5):(c==2)?vec2(0.5,0.5):(c==3)?vec2(-0.5,-0.5):(c==4)?vec2(0.5,0.5):vec2(-0.5,0.5);\n"
    "  vec3 pos;\n"
    "  if (snow) { float s = (0.055 + 0.085*h1(fid+9.0)) * uSize; pos = w + (uRight*q.x + vec3(0.0,1.0,0.0)*q.y) * s; }\n"
    "  else { vec3 rt = normalize(vec3(uRight.x, 0.0, uRight.z) + vec3(0.0001)); pos = w + rt*q.x*0.035*uSize + vec3(0.0,1.0,0.0)*q.y*0.75*uSize; }\n"
    "  gl_Position = uVP * vec4(pos, 1.0);\n"
    "  vUV = q + 0.5;\n"
    "  float d = length(rel);\n"
    "  vA = smoothstep(0.7, 3.5, d) * (1.0 - smoothstep(uBox.x*0.36, uBox.x*0.5, length(rel.xz)));\n"
    "}\n";

static const char *w_frag =
    "#version 330 core\n"
    "in vec2 vUV; in float vA; uniform vec3 uColor; uniform float uType; out vec4 F;\n"
    "void main(){\n"
    "  vec2 p = vUV*2.0-1.0; float a;\n"
    "  if (uType < 1.5) { a = smoothstep(1.0, 0.15, length(p)) * 0.95; }\n"
    "  else { a = smoothstep(1.0, 0.0, abs(p.x)) * smoothstep(1.0, 0.55, abs(p.y)) * 0.5; }\n"
    "  F = vec4(uColor, a * vA);\n"
    "}\n";

static void ensure_w_gl(void) {
    if (g_w_shader) return;
    g_w_shader = g3d_shader_create(w_vert, w_frag);
    glGenVertexArrays(1, &g_w_vao);                 /* sin atributos: los vertices salen de gl_VertexID */
}
#endif

void g3d_weather_render(G3DCamera *camera, int flip_y) {
#ifndef VITA
    if (!camera || g_w_type == 0 || g_w_int <= 0.001f) return;
    ensure_w_gl();
    if (!g_w_shader) return;
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 vp = mat4_multiply(proj, view);
    Vec3 cR = g3d_camera_get_right(camera);
    float time = (float)fmod((double)SDL_GetTicks() / 1000.0, 3600.0);
    float maxn = (g_w_type == 1) ? 8000.0f : 6000.0f;
    float color[3] = { g_w_col[0], g_w_col[1], g_w_col[2] };
    if (!g_w_style_set) {                            /* por defecto: nieve blanca, lluvia azulada */
        if (g_w_type == 1) { color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f; } else { color[0] = 0.75f; color[1] = 0.82f; color[2] = 0.95f; }
    }
    g3d_shader_use(g_w_shader);
    g3d_shader_set_mat4(g_w_shader, "uVP", vp);
    g3d_shader_set_vec3(g_w_shader, "uCam", camera->position);
    g3d_shader_set_vec3(g_w_shader, "uRight", cR);
    g3d_shader_set_float(g_w_shader, "uTime", time);
    g3d_shader_set_float(g_w_shader, "uCount", maxn * g_w_int);
    g3d_shader_set_vec3(g_w_shader, "uWind", vec3_make(g_w_wind[0], 0.0f, g_w_wind[1]));
    g3d_shader_set_float(g_w_shader, "uType", (float)g_w_type);
    g3d_shader_set_float(g_w_shader, "uSize", g_w_size);
    g3d_shader_set_float(g_w_shader, "uSpeed", g_w_speed);
    g3d_shader_set_vec3(g_w_shader, "uBox", vec3_make(g_w_area, g_w_area * 0.6f, g_w_area));
    g3d_shader_set_vec3(g_w_shader, "uColor", vec3_make(color[0], color[1], color[2]));
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glBindVertexArray(g_w_vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(WEATHER_MAX * 6));
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
#else
    (void)camera; (void)flip_y;
#endif
}
