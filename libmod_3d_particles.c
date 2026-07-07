/*
 * libmod_3d_particles.c - point-sprite particle system
 */

#include "libmod_3d_particles.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif

#define G3D_MAX_EMITTERS 16

/* ---- shaders ----------------------------------------------------------- */

static const char *part_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in float life;\n" /* 0..1 remaining */
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "uniform float uSize;\n"
    "uniform float uViewportH;\n"
    "out float vLife;\n"
    "void main() {\n"
    "    vLife = life;\n"
    "    vec4 vpos = uView * vec4(position, 1.0);\n"
    "    gl_Position = uProjection * vpos;\n"
    "    /* perspective point size: world size projected to pixels */\n"
    "    float d = max(-vpos.z, 0.1);\n"
    "    gl_PointSize = clamp(uSize * uViewportH / d, 1.0, 256.0);\n"
    "}\n";

static const char *part_frag =
    "#version 330 core\n"
    "in float vLife;\n"
    "uniform vec3 uColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    vec2 p = gl_PointCoord * 2.0 - 1.0;\n"
    "    float r = dot(p, p);\n"
    "    if (r > 1.0) discard;\n"            /* round droplet */
    "    float soft = 1.0 - r;\n"
    "    float alpha = soft * soft * clamp(vLife * 1.5, 0.0, 1.0);\n"
    "    FragColor = vec4(uColor, alpha);\n"
    "}\n";

/* ---- state ------------------------------------------------------------- */

typedef struct {
    float px, py, pz;
    float vx, vy, vz;
    float life;     /* seconds remaining */
} Particle;

typedef struct {
    int active;
    float ox, oy, oz;       /* spawn box centre */
    float ex, ey, ez;       /* spawn box half-extents */
    float vx, vy, vz;       /* base velocity */
    float spread;           /* random velocity spread */
    float gravity;
    float size;
    float life;             /* full lifetime */
    float floor_y;          /* particles below this respawn (vanish) */
    float color[3];
    int count;
    Particle *pool;
    float *buffer;          /* interleaved pos(3)+life01(1) for the VBO */
    unsigned int vao, vbo;
} Emitter;

static struct {
    int initialized;
    G3DShaderProgram *shader;
    Emitter emitters[G3D_MAX_EMITTERS];
    int count;
    unsigned int last_ticks;
} g_part = {0};

/* ---- one-shot droplet bursts (splashes) --------------------------------
   A shared transient pool that does not recycle: each droplet flies, falls and
   then frees its slot. Drawn with the same point-sprite shader. */
#define G3D_MAX_DROPS 1024
static struct {
    float px, py, pz, vx, vy, vz, life, max_life;
    int active;
} g_drops[G3D_MAX_DROPS];
static int g_drops_inited = 0;
static unsigned int g_drops_vao = 0, g_drops_vbo = 0;
static float *g_drops_buf = NULL;
static float g_drops_size = 0.12f;
static float g_drops_color[3] = {0.85f, 0.92f, 1.0f};
static const float G3D_DROP_GRAVITY = 9.8f;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float frand2(void) { return frand() * 2.0f - 1.0f; }

static void respawn(Emitter *e, Particle *p) {
    p->px = e->ox + e->ex * frand2();
    p->py = e->oy + e->ey * frand2();
    p->pz = e->oz + e->ez * frand2();
    p->vx = e->vx + e->spread * frand2();
    p->vy = e->vy + e->spread * frand2();
    p->vz = e->vz + e->spread * frand2();
    p->life = e->life * (0.4f + 0.6f * frand());
}

int g3d_particles_create(float x, float y, float z, float ex, float ey,
                         float ez, float vx, float vy, float vz, float spread,
                         float gravity, float size, float life, int count) {
#ifndef VITA
    if (!g_part.initialized) {
        g_part.shader = g3d_shader_create(part_vert, part_frag);
        g_part.initialized = 1;
    }
    if (!g_part.shader || g_part.count >= G3D_MAX_EMITTERS || count <= 0)
        return -1;
    if (count > 20000) count = 20000;

    Emitter *e = &g_part.emitters[g_part.count];
    e->ox = x; e->oy = y; e->oz = z;
    e->ex = ex; e->ey = ey; e->ez = ez;
    e->vx = vx; e->vy = vy; e->vz = vz;
    e->spread = spread; e->gravity = gravity;
    e->size = size; e->life = life; e->count = count;
    e->floor_y = -1.0e9f; /* no floor by default */
    e->color[0] = 1.0f; e->color[1] = 1.0f; e->color[2] = 1.0f;
    e->pool = (Particle *)malloc((size_t)count * sizeof(Particle));
    e->buffer = (float *)malloc((size_t)count * 4 * sizeof(float));
    if (!e->pool || !e->buffer) { free(e->pool); free(e->buffer); return -1; }

    /* Stagger initial lifetimes so the stream is continuous from the start */
    for (int i = 0; i < count; i++) {
        respawn(e, &e->pool[i]);
        e->pool[i].life = life * frand();
    }

    glGenVertexArrays(1, &e->vao);
    glBindVertexArray(e->vao);
    glGenBuffers(1, &e->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, e->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)(count * 4 * sizeof(float)), NULL,
                 GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glBindVertexArray(0);

    e->active = 1;
    return g_part.count++;
#else
    return -1;
#endif
}

void g3d_particles_set_color(int id, float r, float g, float b) {
    if (id < 0 || id >= g_part.count)
        return;
    g_part.emitters[id].color[0] = r;
    g_part.emitters[id].color[1] = g;
    g_part.emitters[id].color[2] = b;
}

void g3d_particles_set_floor(int id, float y) {
    if (id < 0 || id >= g_part.count)
        return;
    g_part.emitters[id].floor_y = y;
}

void g3d_particles_burst(float x, float y, float z, int count, float speed,
                         float size, float life, float r, float g, float b) {
#ifndef VITA
    if (count <= 0) return;
    g_drops_size = size;
    g_drops_color[0] = r; g_drops_color[1] = g; g_drops_color[2] = b;
    int spawned = 0;
    for (int i = 0; i < G3D_MAX_DROPS && spawned < count; i++) {
        if (g_drops[i].active) continue;
        float ang = frand() * 6.2831853f;
        float horiz = speed * (0.3f + 0.7f * frand());
        g_drops[i].px = x; g_drops[i].py = y; g_drops[i].pz = z;
        g_drops[i].vx = cosf(ang) * horiz;
        g_drops[i].vz = sinf(ang) * horiz;
        g_drops[i].vy = speed * (0.7f + 0.6f * frand());
        g_drops[i].max_life = life * (0.6f + 0.4f * frand());
        g_drops[i].life = g_drops[i].max_life;
        g_drops[i].active = 1;
        spawned++;
    }
#else
    (void)x; (void)y; (void)z; (void)count; (void)speed;
    (void)size; (void)life; (void)r; (void)g; (void)b;
#endif
}

void g3d_particles_update_render(G3DCamera *camera, int flip_y) {
    if (!camera) return;

    int any_drops = 0;
    for (int i = 0; i < G3D_MAX_DROPS; i++)
        if (g_drops[i].active) { any_drops = 1; break; }

    if (g_part.count == 0 && !any_drops)
        return;
    if (!g_part.initialized) {
        g_part.shader = g3d_shader_create(part_vert, part_frag);
        g_part.initialized = 1;
    }
    if (!g_part.shader)
        return;

#ifndef VITA
    /* dt from the wall clock (capped) */
    unsigned int now = SDL_GetTicks();
    float dt = (g_part.last_ticks > 0) ? (now - g_part.last_ticks) / 1000.0f : 0.016f;
    g_part.last_ticks = now;
    if (dt > 0.05f) dt = 0.05f;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }

    g3d_shader_use(g_part.shader);
    g3d_shader_set_mat4(g_part.shader, "uView", view);
    g3d_shader_set_mat4(g_part.shader, "uProjection", proj);
    g3d_shader_set_float(g_part.shader, "uViewportH", 540.0f);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for (int ei = 0; ei < g_part.count; ei++) {
        Emitter *e = &g_part.emitters[ei];
        if (!e->active)
            continue;

        int n = 0;
        for (int i = 0; i < e->count; i++) {
            Particle *p = &e->pool[i];
            p->life -= dt;
            if (p->life <= 0.0f || p->py < e->floor_y) {
                respawn(e, p);
            } else {
                p->vy -= e->gravity * dt;
                p->px += p->vx * dt;
                p->py += p->vy * dt;
                p->pz += p->vz * dt;
            }
            e->buffer[n++] = p->px;
            e->buffer[n++] = p->py;
            e->buffer[n++] = p->pz;
            e->buffer[n++] = p->life / e->life; /* 0..1 */
        }

        glBindBuffer(GL_ARRAY_BUFFER, e->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (long)(e->count * 4 * sizeof(float)),
                        e->buffer);
        g3d_shader_set_float(g_part.shader, "uSize", e->size);
        g3d_shader_set_vec3(g_part.shader, "uColor",
                            vec3_make(e->color[0], e->color[1], e->color[2]));
        glBindVertexArray(e->vao);
        glDrawArrays(GL_POINTS, 0, e->count);
    }

    /* one-shot droplet bursts (splashes): integrate + draw, free dead slots */
    if (any_drops) {
        if (!g_drops_inited) {
            glGenVertexArrays(1, &g_drops_vao);
            glBindVertexArray(g_drops_vao);
            glGenBuffers(1, &g_drops_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, g_drops_vbo);
            glBufferData(GL_ARRAY_BUFFER, G3D_MAX_DROPS * 4 * sizeof(float), NULL,
                         GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  (void *)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                  (void *)(3 * sizeof(float)));
            glBindVertexArray(0);
            g_drops_buf = (float *)malloc(G3D_MAX_DROPS * 4 * sizeof(float));
            g_drops_inited = 1;
        }
        int n = 0;
        for (int i = 0; i < G3D_MAX_DROPS; i++) {
            if (!g_drops[i].active) continue;
            g_drops[i].life -= dt;
            if (g_drops[i].life <= 0.0f) { g_drops[i].active = 0; continue; }
            g_drops[i].vy -= G3D_DROP_GRAVITY * dt;
            g_drops[i].px += g_drops[i].vx * dt;
            g_drops[i].py += g_drops[i].vy * dt;
            g_drops[i].pz += g_drops[i].vz * dt;
            g_drops_buf[n++] = g_drops[i].px;
            g_drops_buf[n++] = g_drops[i].py;
            g_drops_buf[n++] = g_drops[i].pz;
            g_drops_buf[n++] = g_drops[i].life / g_drops[i].max_life;
        }
        if (n > 0 && g_drops_buf) {
            glBindBuffer(GL_ARRAY_BUFFER, g_drops_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, (long)(n * sizeof(float)),
                            g_drops_buf);
            g3d_shader_set_float(g_part.shader, "uSize", g_drops_size);
            g3d_shader_set_vec3(g_part.shader, "uColor",
                                vec3_make(g_drops_color[0], g_drops_color[1],
                                          g_drops_color[2]));
            glBindVertexArray(g_drops_vao);
            glDrawArrays(GL_POINTS, 0, n / 4);
        }
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

void g3d_particles_clear(void) {
#ifndef VITA
    for (int i = 0; i < g_part.count; i++) {
        Emitter *e = &g_part.emitters[i];
        if (e->vao) glDeleteVertexArrays(1, &e->vao);
        if (e->vbo) glDeleteBuffers(1, &e->vbo);
        free(e->pool); free(e->buffer);
        e->pool = NULL; e->buffer = NULL; e->active = 0;
        e->vao = e->vbo = 0;
    }
#endif
    g_part.count = 0;
}

void g3d_particles_shutdown(void) {
    g3d_particles_clear();
#ifndef VITA
    if (g_drops_vao) glDeleteVertexArrays(1, &g_drops_vao);
    if (g_drops_vbo) glDeleteBuffers(1, &g_drops_vbo);
    free(g_drops_buf); g_drops_buf = NULL;
    g_drops_vao = g_drops_vbo = 0; g_drops_inited = 0;
    for (int i = 0; i < G3D_MAX_DROPS; i++) g_drops[i].active = 0;
#endif
    if (g_part.shader) {
        g3d_shader_free(g_part.shader);
        g_part.shader = NULL;
    }
    g_part.initialized = 0;
}
