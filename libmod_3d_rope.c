/*
 * libmod_3d_rope.c - Cuerdas (Verlet + distancias fijas), dibujadas como tubo.
 *
 * Es la hermana pequena de la tela: las mismas particulas y las mismas reglas,
 * pero en una fila. Se dibuja como un tubo de pocos lados en vez de una linea,
 * porque una linea de un pixel desaparece de canto y no recibe luz.
 */

#include "libmod_3d_rope.h"
#include "libmod_3d_cloth.h"     /* comparte los empujones (g3d_push_apply) */
#include "libmod_3d_mesh.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_material.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_math.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>

#define MAX_ROPES 32
#define LADOS     6          /* lados del tubo: con 6 ya parece redonda */
#define ITERS     8          /* relajaciones por frame: una cuerda estira poco */

typedef struct {
    int active;
    int n;                   /* particulas */
    float seg;               /* longitud de cada tramo */
    float radio;
    Vec3 *pos, *prev, *pinPos;
    unsigned char *pinned;
    G3DMesh *mesh;
    int entity, material;
    Vec3 wind; float windStr;
    float gravity, damp;
} Rope;

static Rope g_ropes[MAX_ROPES];

static Rope *get(int id) {
    if (id < 0 || id >= MAX_ROPES || !g_ropes[id].active) return NULL;
    return &g_ropes[id];
}

int g3d_rope_create(float ax, float ay, float az,
                    float bx, float by, float bz,
                    int segmentos, float grosor, float holgura) {
    int id = -1;
    for (int i = 0; i < MAX_ROPES; i++) if (!g_ropes[i].active) { id = i; break; }
    if (id < 0) return -1;
    if (segmentos < 2) segmentos = 2;
    if (segmentos > 400) segmentos = 400;
    if (grosor <= 0.0f) grosor = 0.05f;
    if (holgura < 0.0f) holgura = 0.0f;

    Rope *r = &g_ropes[id];
    memset(r, 0, sizeof(*r));
    r->n = segmentos + 1;
    r->radio = grosor;
    r->gravity = 9.8f;
    r->damp = 0.995f;

    Vec3 a = vec3_make(ax, ay, az), b = vec3_make(bx, by, bz);
    Vec3 ab = vec3_sub(b, a);
    float dist = sqrtf(ab.x * ab.x + ab.y * ab.y + ab.z * ab.z);
    if (dist < 1e-4f) { ab = vec3_make(0.0f, -1.0f, 0.0f); dist = 1.0f; }
    /* la cuerda mide MAS que la distancia entre extremos: por eso cuelga */
    r->seg = (dist * (1.0f + holgura)) / segmentos;

    r->pos    = (Vec3 *)malloc(r->n * sizeof(Vec3));
    r->prev   = (Vec3 *)malloc(r->n * sizeof(Vec3));
    r->pinPos = (Vec3 *)malloc(r->n * sizeof(Vec3));
    r->pinned = (unsigned char *)calloc(r->n, 1);
    for (int i = 0; i < r->n; i++) {
        float t = (float)i / (r->n - 1);
        Vec3 p = vec3_add(a, vec3_scale(ab, t));
        /* se empieza ya con algo de panza, asi no da el latigazo del primer frame */
        p.y -= sinf(t * 3.14159265f) * dist * holgura * 0.5f;
        r->pos[i] = r->prev[i] = r->pinPos[i] = p;
    }
    /* los dos extremos, fijos: es lo normal en una cuerda tendida */
    r->pinned[0] = 1;
    r->pinned[r->n - 1] = 1;

    /* ---- la malla: un anillo de LADOS vertices por particula ---- */
    int nv = r->n * LADOS;
    int ni = (r->n - 1) * LADOS * 6;
    G3DVertex *verts = (G3DVertex *)calloc(nv, sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)ni * sizeof(uint32_t));
    int ic = 0;
    for (int i = 0; i < r->n - 1; i++)
        for (int l = 0; l < LADOS; l++) {
            int l2 = (l + 1) % LADOS;
            int a0 = i * LADOS + l,  a1 = i * LADOS + l2;
            int b0 = (i + 1) * LADOS + l, b1 = (i + 1) * LADOS + l2;
            idx[ic++] = a0; idx[ic++] = b0; idx[ic++] = a1;
            idx[ic++] = a1; idx[ic++] = b0; idx[ic++] = b1;
        }
    r->mesh = g3d_mesh_create("rope", verts, nv, idx, ic);
    free(verts); free(idx);
    if (!r->mesh) { free(r->pos); free(r->prev); free(r->pinPos); free(r->pinned); return -1; }
    g3d_mesh_upload_gpu(r->mesh);

    r->material = g3d_material_impl_create();
    g3d_material_impl_set_color(r->material, 1, 1, 1, 1);

    int scene = g3d_scene_impl_get_active();
    r->entity = g3d_entity_impl_spawn(scene, 0, 0, 0, 0);
    G3DEntity *ent = g3d_entity_impl_get(r->entity);
    if (ent) { ent->mesh = r->mesh; ent->material_id = r->material; }

    r->active = 1;
    return id;
}

void g3d_rope_pin(int rope, int extremo, int fijo) {
    Rope *r = get(rope); if (!r) return;
    int i = extremo ? r->n - 1 : 0;
    r->pinned[i] = fijo ? 1 : 0;
    if (fijo) r->pinPos[i] = r->pos[i];
}

void g3d_rope_pin_move(int rope, int punto, float x, float y, float z) {
    Rope *r = get(rope); if (!r) return;
    if (punto < 0 || punto >= r->n) return;
    r->pinned[punto] = 1;
    r->pinPos[punto] = vec3_make(x, y, z);
}

void g3d_rope_set_wind(int rope, float x, float y, float z, float strength) {
    Rope *r = get(rope); if (!r) return;
    r->wind = vec3_make(x, y, z); r->windStr = strength;
}

void g3d_rope_set_texture(int rope, unsigned int gl_handle) {
    Rope *r = get(rope); if (!r) return;
    G3DMaterial *m = g3d_material_impl_get(r->material);
    if (!m) return;
    /* igual que la tela: se envuelve el handle de GL en una textura ligera para
       que el pintado la pueda enganchar */
    static G3DTexture wrap[MAX_ROPES];
    wrap[rope].gl_handle = gl_handle;
    wrap[rope].gpu_uploaded = 1;
    m->albedo_texture = gl_handle ? &wrap[rope] : NULL;
}

int g3d_rope_points(int rope) { Rope *r = get(rope); return r ? r->n : 0; }

int g3d_rope_point(int rope, int punto, float *x, float *y, float *z) {
    Rope *r = get(rope); if (!r) return 0;
    if (punto < 0 || punto >= r->n) return 0;
    if (x) *x = r->pos[punto].x;
    if (y) *y = r->pos[punto].y;
    if (z) *z = r->pos[punto].z;
    return 1;
}

/* Acerca dos particulas a su distancia de reposo (la mitad cada una, o toda si
   la otra esta clavada). */
static void satisfy(Rope *r, int a, int b) {
    Vec3 d = vec3_sub(r->pos[b], r->pos[a]);
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return;
    float f = (len - r->seg) / len;
    Vec3 corr = vec3_scale(d, 0.5f * f);
    int pa = r->pinned[a], pb = r->pinned[b];
    if (!pa && !pb) { r->pos[a] = vec3_add(r->pos[a], corr); r->pos[b] = vec3_sub(r->pos[b], corr); }
    else if (!pa && pb) r->pos[a] = vec3_add(r->pos[a], vec3_scale(d, f));
    else if (pa && !pb) r->pos[b] = vec3_sub(r->pos[b], vec3_scale(d, f));
}

void g3d_rope_update(int rope, float dt) {
    Rope *r = get(rope); if (!r) return;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    if (dt > 0.05f) dt = 0.05f;

    /* ---- Verlet: gravedad y viento ---- */
    for (int i = 0; i < r->n; i++) {
        if (r->pinned[i]) { r->pos[i] = r->prev[i] = r->pinPos[i]; continue; }
        Vec3 p = r->pos[i];
        Vec3 v = vec3_scale(vec3_sub(p, r->prev[i]), r->damp);
        Vec3 acc = vec3_make(r->wind.x * r->windStr,
                             -r->gravity + r->wind.y * r->windStr,
                             r->wind.z * r->windStr);
        r->prev[i] = p;
        r->pos[i] = vec3_add(vec3_add(p, v), vec3_scale(acc, dt * dt));
    }

    /* ---- las distancias, varias pasadas ---- */
    for (int it = 0; it < ITERS; it++) {
        for (int i = 0; i < r->n - 1; i++) satisfy(r, i, i + 1);
        for (int i = 0; i < r->n; i++) if (r->pinned[i]) r->pos[i] = r->pinPos[i];
    }

    /* ---- lo que pase la aparta (los mismos empujones que las telas) ---- */
    g3d_push_caducar();
    for (int i = 0; i < r->n; i++) {
        if (r->pinned[i]) continue;
        g3d_push_apply(&r->pos[i].x, &r->pos[i].y, &r->pos[i].z);
    }

    /* ---- el tubo: un anillo por particula, orientado segun la cuerda ---- */
    if (!r->mesh || !r->mesh->vertices) return;
    for (int i = 0; i < r->n; i++) {
        Vec3 t;
        if (i == 0)               t = vec3_sub(r->pos[1], r->pos[0]);
        else if (i == r->n - 1)   t = vec3_sub(r->pos[i], r->pos[i - 1]);
        else                      t = vec3_sub(r->pos[i + 1], r->pos[i - 1]);
        float tl = sqrtf(t.x * t.x + t.y * t.y + t.z * t.z);
        if (tl < 1e-6f) t = vec3_make(0, 1, 0); else t = vec3_scale(t, 1.0f / tl);
        /* un lado cualquiera perpendicular: si la cuerda va casi vertical, se
           coge otro de referencia o el producto vectorial se va a cero */
        Vec3 ref = (fabsf(t.y) > 0.9f) ? vec3_make(1, 0, 0) : vec3_make(0, 1, 0);
        Vec3 s = vec3_make(t.y * ref.z - t.z * ref.y,
                           t.z * ref.x - t.x * ref.z,
                           t.x * ref.y - t.y * ref.x);
        float sl = sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
        if (sl < 1e-6f) s = vec3_make(1, 0, 0); else s = vec3_scale(s, 1.0f / sl);
        Vec3 u = vec3_make(s.y * t.z - s.z * t.y,
                           s.z * t.x - s.x * t.z,
                           s.x * t.y - s.y * t.x);
        for (int l = 0; l < LADOS; l++) {
            float ang = (float)l / LADOS * 6.2831853f;
            float ca = cosf(ang), sa = sinf(ang);
            Vec3 nrm = vec3_add(vec3_scale(s, ca), vec3_scale(u, sa));
            Vec3 p = vec3_add(r->pos[i], vec3_scale(nrm, r->radio));
            G3DVertex *v = &r->mesh->vertices[i * LADOS + l];
            v->position[0] = p.x; v->position[1] = p.y; v->position[2] = p.z;
            v->normal[0] = nrm.x; v->normal[1] = nrm.y; v->normal[2] = nrm.z;
            v->texcoord[0] = (float)l / LADOS;
            v->texcoord[1] = (float)i * r->seg;   /* la textura se repite a lo largo */
        }
    }
    g3d_mesh_update_gpu(r->mesh);
}

void g3d_rope_destroy(int rope) {
    Rope *r = get(rope); if (!r) return;
    free(r->pos); free(r->prev); free(r->pinPos); free(r->pinned);
    r->active = 0;
}

void g3d_rope_shutdown(void) {
    for (int i = 0; i < MAX_ROPES; i++) if (g_ropes[i].active) g3d_rope_destroy(i);
}
