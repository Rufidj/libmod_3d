/*
 * libmod_3d_sprite.c - Sprites 2D en el mundo 3D (ver libmod_3d_sprite.h).
 *
 * El quad se arma en el shader a partir del centro, del "derecha" y "arriba" del
 * encaramiento y del punto de anclaje, asi que un sprite es UN draw de 4
 * vertices sin tocar buffers. Se dibuja con el test de profundidad puesto y
 * escribiendo profundidad, y con recorte alfa (discard): eso hace que el sprite
 * se meta en la escena como un objeto solido mas, sin ordenar transparencias.
 */
#include "libmod_3d_sprite.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include "libmod_3d_light.h"
#include "libmod_3d_renderer.h"

#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPRITE_MAX 4096

typedef struct {
    int   used;
    int   visible;
    int   scene;
    float x, y, z;
    unsigned int tex;
    int   px_w, px_h;             /* tamano del grafico ENTERO, en pixeles */
    float uscale, vscale;         /* por si la textura viene rellenada a potencia de dos */
    int   cell_x, cell_y, cell_w, cell_h;   /* recorte dentro del grafico (0 = entero) */
    int   cols, rows, frame;      /* rejilla de la hoja de sprites */
    float height;                 /* alto en unidades de mundo (0 = usar ppu) */
    float ppu;                    /* pixeles por unidad de mundo */
    float anchor_x, anchor_y;
    int   billboard;
    float cutout;
    float r, g, b, a;
    int   flip_x;
    float scale;                  /* el local 'size' (1.0 = 100%) */
    int   shadow;
    int   smooth;                 /* 0 = pixel art (nearest), 1 = suavizado */
    int   lit;                    /* 1 = le afecta la luz de la escena */
    int   snap;                   /* 1 = ajustar a pixel de pantalla (pixel art) */
} Sprite;

static Sprite g_spr[SPRITE_MAX];
static int    g_spr_top = 0;      /* uno mas alto que el ultimo usado */
static int    g_spr_n   = 0;

#ifndef VITA
static G3DShaderProgram *g_shader = NULL;
static G3DShaderProgram *g_shader_depth = NULL;
static GLuint g_vao = 0, g_vbo = 0;
/* Muestreadores propios: el filtrado NO se toca en la textura, que es la misma
   que usa BennuGD2 para dibujar en 2D y no hay que cambiarsela por detras. */
static GLuint g_smp_nearest = 0, g_smp_linear = 0;

static const char *spr_vert =
    "#version 330 core\n"
    "layout(location=0) in vec2 aCorner;\n"            /* (0,0) arriba-izq .. (1,1) abajo-der */
    "uniform vec3 uCenter; uniform vec3 uRight; uniform vec3 uUp;\n"
    "uniform vec2 uSize; uniform vec2 uAnchor; uniform vec4 uUV; uniform float uFlip;\n"
    "uniform vec3 uSnap;\n"
    "uniform mat4 uViewProj;\n"
    "out vec2 vUV;\n"
    "void main(){\n"
    "  float cx = mix(aCorner.x, 1.0 - aCorner.x, uFlip);\n"
    "  vUV = vec2(mix(uUV.x, uUV.z, cx), mix(uUV.y, uUV.w, aCorner.y));\n"
    "  vec3 wp = uCenter + uRight * ((aCorner.x - uAnchor.x) * uSize.x)\n"
    "                    + uUp    * ((uAnchor.y - aCorner.y) * uSize.y);\n"
    "  vec4 pos = uViewProj * vec4(wp, 1.0);\n"
    /* Ajuste a pixel: un sprite de pixel art que cae entre pixeles de pantalla
       tiembla al moverse (las lineas del dibujo saltan de un pixel a otro). Se
       redondea su posicion en pantalla a pixeles enteros y se acabo el baile.
       uSnap = 0 lo desactiva (util si el sprite es grande o esta rotado). */
    "  if (uSnap.x > 0.5 && pos.w > 0.0) {\n"
    "      vec2 px = (pos.xy / pos.w) * 0.5 * uSnap.yz;\n"
    "      px = floor(px + 0.5);\n"
    "      pos.xy = (px / (0.5 * uSnap.yz)) * pos.w;\n"
    "  }\n"
    "  gl_Position = pos;\n"
    "}\n";

static const char *spr_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uTex; uniform vec4 uTint; uniform float uCutout;\n"
    "uniform vec3 uLuz;\n"                     /* luz de la escena (ambiente + sol) */
    "void main(){\n"
    "  vec4 c = texture(uTex, vUV);\n"
    "  if (c.a < uCutout) discard;\n"          /* recorte: ni mezcla ni orden */
    "  F = vec4(c.rgb * uTint.rgb * uLuz, c.a * uTint.a);\n"
    "}\n";

static const char *spr_frag_depth =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uTex; uniform float uCutout;\n"
    "void main(){ if (texture(uTex, vUV).a < uCutout) discard; }\n";

static void ensure_gl(void) {
    if (g_vao) return;
    g_shader       = g3d_shader_create(spr_vert, spr_frag);
    g_shader_depth = g3d_shader_create(spr_vert, spr_frag_depth);
    /* esquinas: (0,0) arriba-izquierda del grafico, (1,1) abajo-derecha */
    float quad[8] = { 0.0f,1.0f,  1.0f,1.0f,  0.0f,0.0f,  1.0f,0.0f };
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);

    glGenSamplers(1, &g_smp_nearest);
    glSamplerParameteri(g_smp_nearest, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(g_smp_nearest, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(g_smp_nearest, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(g_smp_nearest, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenSamplers(1, &g_smp_linear);
    glSamplerParameteri(g_smp_linear, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(g_smp_linear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(g_smp_linear, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(g_smp_linear, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
#endif

/* --------------------------------------------------------------------------- */

static Sprite *get(int id) {
    if (id < 0 || id >= SPRITE_MAX || !g_spr[id].used) return NULL;
    return &g_spr[id];
}

int g3d_sprite_create(int scene_id, float x, float y, float z) {
    for (int i = 0; i < SPRITE_MAX; i++) {
        if (g_spr[i].used) continue;
        Sprite *s = &g_spr[i];
        memset(s, 0, sizeof(*s));
        s->used = 1; s->visible = 1; s->scene = scene_id;
        s->x = x; s->y = y; s->z = z;
        s->anchor_x = 0.5f; s->anchor_y = 1.0f;   /* los pies en x,y,z */
        s->billboard = G3D_SPRITE_CYLINDRICAL;
        s->cutout = 0.5f;
        s->r = s->g = s->b = s->a = 1.0f;
        s->ppu = 32.0f;                           /* 32 px = 1 unidad, hasta que digan otra cosa */
        s->scale = 1.0f;
        s->shadow = 1;
        s->lit = 1;                               /* que pertenezca al mundo */
        s->snap = 1;                              /* pixel art quieto, sin temblar */
        s->uscale = s->vscale = 1.0f;
        if (i >= g_spr_top) g_spr_top = i + 1;
        g_spr_n++;
        return i;
    }
    return -1;
}

void g3d_sprite_destroy(int id) {
    Sprite *s = get(id);
    if (!s) return;
    s->used = 0;
    g_spr_n--;
    while (g_spr_top > 0 && !g_spr[g_spr_top - 1].used) g_spr_top--;
}

void g3d_sprites_clear(void) {
    memset(g_spr, 0, sizeof(g_spr));
    g_spr_top = 0; g_spr_n = 0;
}

int g3d_sprite_count(void) { return g_spr_n; }
int g3d_sprite_alive(int id) { return get(id) != NULL; }

void g3d_sprite_set_position(int id, float x, float y, float z) {
    Sprite *s = get(id); if (!s) return;
    s->x = x; s->y = y; s->z = z;
}

void g3d_sprite_get_position(int id, float *x, float *y, float *z) {
    Sprite *s = get(id); if (!s) return;
    if (x) *x = s->x; if (y) *y = s->y; if (z) *z = s->z;
}

void g3d_sprite_set_texture(int id, unsigned int gl_tex, int px_w, int px_h,
                            float uscale, float vscale) {
    Sprite *s = get(id); if (!s) return;
    s->tex = gl_tex; s->px_w = px_w; s->px_h = px_h;
    s->uscale = uscale; s->vscale = vscale;
}

/* Recorte en pixeles dentro del grafico. Con w o h a 0 se usa el grafico entero
   (que es lo que pasa con un FPG, donde cada fotograma ya es su propio grafico). */
void g3d_sprite_set_cell(int id, int x, int y, int w, int h) {
    Sprite *s = get(id); if (!s) return;
    s->cell_x = x; s->cell_y = y; s->cell_w = w; s->cell_h = h;
    s->cols = s->rows = 0;
}

/* Hoja de sprites en rejilla: columnas x filas dentro del MISMO grafico. */
void g3d_sprite_set_grid(int id, int cols, int rows) {
    Sprite *s = get(id); if (!s) return;
    s->cols = cols > 0 ? cols : 0;
    s->rows = rows > 0 ? rows : 0;
}

void g3d_sprite_set_frame(int id, int frame) {
    Sprite *s = get(id); if (!s) return;
    s->frame = frame < 0 ? 0 : frame;
}

/* Recorte efectivo (en pixeles) del fotograma que toca. */
static void sprite_cell(const Sprite *s, int *x, int *y, int *w, int *h) {
    if (s->cols > 0 && s->rows > 0 && s->px_w > 0 && s->px_h > 0) {
        int cw = s->px_w / s->cols, ch = s->px_h / s->rows;
        int n  = s->frame % (s->cols * s->rows);
        *x = (n % s->cols) * cw;  *y = (n / s->cols) * ch;
        *w = cw; *h = ch;
        return;
    }
    if (s->cell_w > 0 && s->cell_h > 0) {
        *x = s->cell_x; *y = s->cell_y; *w = s->cell_w; *h = s->cell_h;
        return;
    }
    *x = 0; *y = 0; *w = s->px_w; *h = s->px_h;
}

void g3d_sprite_set_height(int id, float h) { Sprite *s = get(id); if (s) s->height = h; }
void g3d_sprite_set_pixels_per_unit(int id, float p) { Sprite *s = get(id); if (s && p > 0.0f) s->ppu = p; }
void g3d_sprite_set_anchor(int id, float ax, float ay) { Sprite *s = get(id); if (s) { s->anchor_x = ax; s->anchor_y = ay; } }
void g3d_sprite_set_billboard(int id, int m) { Sprite *s = get(id); if (s) s->billboard = m; }
void g3d_sprite_set_cutout(int id, float t) { Sprite *s = get(id); if (s) s->cutout = t; }
void g3d_sprite_set_flip(int id, int f) { Sprite *s = get(id); if (s) s->flip_x = f ? 1 : 0; }
void g3d_sprite_set_scale(int id, float sc) { Sprite *s = get(id); if (s && sc > 0.0f) s->scale = sc; }
void g3d_sprite_set_visible(int id, int v) { Sprite *s = get(id); if (s) s->visible = v ? 1 : 0; }
void g3d_sprite_set_shadow(int id, int on) { Sprite *s = get(id); if (s) s->shadow = on ? 1 : 0; }
void g3d_sprite_set_smooth(int id, int on) { Sprite *s = get(id); if (s) s->smooth = on ? 1 : 0; }
void g3d_sprite_set_lit(int id, int on) { Sprite *s = get(id); if (s) s->lit = on ? 1 : 0; }
void g3d_sprite_set_snap(int id, int on) { Sprite *s = get(id); if (s) s->snap = on ? 1 : 0; }

void g3d_sprite_set_tint(int id, float r, float g, float b, float a) {
    Sprite *s = get(id); if (!s) return;
    s->r = r; s->g = g; s->b = b; s->a = a;
}

/* Tamano del quad en unidades de mundo. */
static void sprite_size(const Sprite *s, float *w, float *h) {
    int cx, cy, cw, ch;
    sprite_cell(s, &cx, &cy, &cw, &ch);
    float pw = (float)(cw > 0 ? cw : 1);
    float ph = (float)(ch > 0 ? ch : 1);
    if (s->height > 0.0f) {
        *h = s->height * s->scale;
        *w = *h * (pw / ph);
    } else {
        *h = (ph / s->ppu) * s->scale;
        *w = (pw / s->ppu) * s->scale;
    }
}

int g3d_sprite_direction(int id, float angle_rad, int ndirs, G3DCamera *camera) {
    Sprite *s = get(id);
    if (!s || !camera || ndirs <= 0) return 0;
    /* Rumbo de la camara hacia el sprite, en el mismo convenio que usa el motor
       para 'angle': 0 = +Z y girando hacia +X. */
    float vx = s->x - camera->position.x;
    float vz = s->z - camera->position.z;
    float camh = atan2f(vx, vz);
    float twopi = (float)(2.0 * M_PI);
    /* rel = 0 cuando el sprite mira hacia la camara -> postura 0 = de frente */
    float rel = angle_rad - camh + (float)M_PI;
    rel = fmodf(rel, twopi);
    if (rel < 0.0f) rel += twopi;
    int idx = (int)floorf(rel / twopi * (float)ndirs + 0.5f);
    return idx % ndirs;
}

/* --------------------------------------------------------------------------- */

#ifndef VITA
/* Dibuja todos los sprites con el shader ya activo. */
/* Cuanta luz hay en la escena: la ambiental mas el sol (las direccionales). Un
   billboard no tiene normal de verdad, asi que se le aplica plano -- es lo que
   hacen los juegos HD-2D -- pero asi el personaje se apaga de noche con el ciclo
   de dia, en vez de quedarse brillando como una pegatina. */
static Vec3 luz_de_la_escena(void) {
    Vec3 luz;
    float amb[3] = { 0.0f, 0.0f, 0.0f }, ai = 0.0f;
    g3d_renderer_get_ambient(amb, &ai);
    luz = vec3_make(amb[0] * ai, amb[1] * ai, amb[2] * ai);
    luz = vec3_scale(luz, 0.4f);
    {
        int n = 0, *ids = g3d_light_impl_get_all(&n), i;
        for (i = 0; i < n; i++) {
            G3DLight *l = g3d_light_impl_get(ids[i]);
            if (!l || !l->active || l->type != 0) continue;   /* solo el sol */
            luz.x += l->color[0] * l->intensity * 0.70f;
            luz.y += l->color[1] * l->intensity * 0.70f;
            luz.z += l->color[2] * l->intensity * 0.70f;
        }
    }
    /* Tope en 1: con un dia normal el sprite sale con SU color (no lavado) y solo
       se oscurece cuando la escena se apaga. */
    if (luz.x > 1.0f) luz.x = 1.0f;
    if (luz.y > 1.0f) luz.y = 1.0f;
    if (luz.z > 1.0f) luz.z = 1.0f;
    return luz;
}

static void draw_all(G3DShaderProgram *sh, Mat4 vp, Vec3 right, Vec3 up,
                     int only_shadow, int billboard_filter) {
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uViewProj", vp);
    g3d_shader_set_vec3(sh, "uRight", right);
    g3d_shader_set_vec3(sh, "uUp", up);
    g3d_shader_set_int(sh, "uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(g_vao);
    for (int i = 0; i < g_spr_top; i++) {
        Sprite *s = &g_spr[i];
        if (!s->used || !s->visible || !s->tex) continue;
        if (only_shadow && !s->shadow) continue;
        if (billboard_filter >= 0 && s->billboard != billboard_filter) continue;
        float w, h; sprite_size(s, &w, &h);
        g3d_shader_set_vec3(sh, "uCenter", vec3_make(s->x, s->y, s->z));
        g3d_shader_set_vec2(sh, "uSize", w, h);
        g3d_shader_set_vec2(sh, "uAnchor", s->anchor_x, s->anchor_y);
        {   /* recorte del fotograma -> coordenadas de textura */
            int cx, cy, cw, ch;
            sprite_cell(s, &cx, &cy, &cw, &ch);
            float fw = (float)(s->px_w > 0 ? s->px_w : 1);
            float fh = (float)(s->px_h > 0 ? s->px_h : 1);
            float us = s->uscale > 0.0f ? s->uscale : 1.0f;
            float vs = s->vscale > 0.0f ? s->vscale : 1.0f;
            g3d_shader_set_vec4(sh, "uUV",
                vec4_make(cx / fw * us, cy / fh * vs,
                          (cx + cw) / fw * us, (cy + ch) / fh * vs));
        }
        g3d_shader_set_float(sh, "uFlip", s->flip_x ? 1.0f : 0.0f);
        {   GLint vp4[4] = { 0, 0, 0, 0 };
            glGetIntegerv(GL_VIEWPORT, vp4);
            g3d_shader_set_vec3(sh, "uSnap",
                                vec3_make((s->snap && !only_shadow) ? 1.0f : 0.0f,
                                          (float)(vp4[2] > 0 ? vp4[2] : 1),
                                          (float)(vp4[3] > 0 ? vp4[3] : 1)));
        }
        g3d_shader_set_float(sh, "uCutout", s->cutout);
        if (!only_shadow) {
            g3d_shader_set_vec4(sh, "uTint", vec4_make(s->r, s->g, s->b, s->a));
            g3d_shader_set_vec3(sh, "uLuz", s->lit ? luz_de_la_escena()
                                                   : vec3_make(1.0f, 1.0f, 1.0f));
        }
        glBindSampler(0, s->smooth ? g_smp_linear : g_smp_nearest);
        glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindVertexArray(0);
    /* El muestreador se desata SIEMPRE. Si se queda puesto en la unidad 0, todo
       lo que se dibuje despues en el frame (terreno, cielo, agua, el resuelto
       HDR) se muestrea con el filtro del sprite -- que es justo lo que pasaba:
       el mundo entero salia pixelado por el 'nearest' de los sprites. */
    glBindSampler(0, 0);
}
#endif

void g3d_sprites_render(G3DCamera *camera, int flip_y) {
#ifndef VITA
    if (!camera || g_spr_n == 0) return;
    ensure_gl();
    if (!g_shader) return;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 vp = mat4_multiply(proj, view);

    Vec3 fwd = g3d_camera_get_forward(camera);
    /* Cilindrico: el sprite se queda de pie aunque la camara mire desde arriba.
       Esferico: se orienta entero hacia la camara (efectos, items). */
    Vec3 up_c    = vec3_make(0.0f, 1.0f, 0.0f);
    /* cross(fwd, up) y NO cross(up, fwd): al reves sale el sprite en espejo
       (en el fuego no se notaba porque una llama espejada es igual). */
    Vec3 right_c = vec3_normalize(vec3_cross(fwd, up_c));
    Vec3 right_s = vec3_make(view.m[0], view.m[4], view.m[8]);
    Vec3 up_s    = vec3_make(view.m[1], view.m[5], view.m[9]);

    /* Se guarda el estado y se devuelve al salir: este pase va EN MEDIO del
       frame, y lo que se deje puesto se lo come todo lo que venga detras. */
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_cull  = glIsEnabled(GL_CULL_FACE);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLint prev_depth_mask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &prev_depth_mask);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);        /* opaco: escribe profundidad y se ordena solo */
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    /* Dos pasadas, una por modo, para no reorientar sprite a sprite. */
    draw_all(g_shader, vp, right_c, up_c, 0, G3D_SPRITE_CYLINDRICAL);
    draw_all(g_shader, vp, right_s, up_s, 0, G3D_SPRITE_SPHERICAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prev_cull)  glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask((GLboolean)prev_depth_mask);
#else
    (void)camera; (void)flip_y;
#endif
}

void g3d_sprites_render_depth(const float *light_view_proj16) {
#ifndef VITA
    if (!light_view_proj16 || g_spr_n == 0) return;
    ensure_gl();
    if (!g_shader_depth) return;
    Mat4 vp;
    memcpy(vp.m, light_view_proj16, 16 * sizeof(float));
    /* Para la sombra el quad se pone de cara a la luz, que es lo que recorta la
       silueta; de pie, como el sprite. */
    Vec3 up = vec3_make(0.0f, 1.0f, 0.0f);
    Vec3 lf = vec3_make(-light_view_proj16[2], -light_view_proj16[6], -light_view_proj16[10]);
    if (vec3_length(lf) < 1e-5f) lf = vec3_make(0.0f, 0.0f, 1.0f);
    lf = vec3_normalize(lf);
    Vec3 right = vec3_cross(lf, up);
    if (vec3_length(right) < 1e-4f) right = vec3_make(1.0f, 0.0f, 0.0f);
    right = vec3_normalize(right);
    GLboolean prev_cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    draw_all(g_shader_depth, vp, right, up, 1, -1);
    if (prev_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
#else
    (void)light_view_proj16;
#endif
}

void g3d_sprites_shutdown(void) {
#ifndef VITA
    if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
    if (g_smp_nearest) { glDeleteSamplers(1, &g_smp_nearest); g_smp_nearest = 0; }
    if (g_smp_linear)  { glDeleteSamplers(1, &g_smp_linear);  g_smp_linear = 0; }
    if (g_shader) { g3d_shader_free(g_shader); g_shader = NULL; }
    if (g_shader_depth) { g3d_shader_free(g_shader_depth); g_shader_depth = NULL; }
#endif
    g3d_sprites_clear();
}
