/*
 * libmod_3d_cloth.c - Cloth simulation (Verlet + distance constraints)
 */

#include "libmod_3d_cloth.h"
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

#define MAX_CLOTHS 32
#define ITERS 7

typedef struct {
    int active;
    int nx, ny;
    float dx, dy, diag;
    Vec3 *pos, *prev, *pinPos;
    unsigned char *pinned;
    G3DMesh *mesh;
    int entity, material;
    Vec3 wind; float windStr;
    Vec3 colPos; float colR; int hasCol;
    float gravity, damp;
} Cloth;

static Cloth g_cloths[MAX_CLOTHS];

/* ---- EMPUJONES: cualquier cosa puede apartar una tela ----
   Un objeto (el jugador, un barril que rueda, un coche) llama a g3d_cloth_push()
   una vez por frame con donde esta y su grosor, y TODAS las telas lo notan. Se
   guardan con su marca de tiempo y se olvidan solos en cuanto el que empujaba
   deja de llamar, asi que no hay que dar de baja a nadie ni saber ids. */
#define MAX_PUSH 32
/* Un empujon es una CAPSULA: dos puntos y un radio. Con una esfera sola, lo que
   sobresalia de ella (la parte de arriba de un barril, la cabeza de alguien)
   atravesaba la tela; con la capsula la tela se apoya en todo el cuerpo. Una
   esfera es una capsula con los dos puntos iguales. */
typedef struct { Vec3 a, b; float r; Uint32 t; } ClothPush;
static ClothPush g_push[MAX_PUSH];
/* cuantas particulas aparto cada empujon en el ultimo frame: es lo que devuelve
   g3d_cloth_push(), para que quien empuja sepa que ESTA tocando tela y pueda
   frenarse (una lona pesada no se atraviesa como una cortina) */
static int       g_push_hit[MAX_PUSH];
static int       g_push_n = 0;

int g3d_cloth_push_capsule(float ax, float ay, float az,
                           float bx, float by, float bz, float radius) {
    if (radius <= 0.0f) return 0;
    Uint32 ahora = SDL_GetTicks();
    Vec3 a = vec3_make(ax, ay, az), b = vec3_make(bx, by, bz);
    /* si ese mismo sitio ya estaba empujando, se refresca en vez de crecer */
    for (int i = 0; i < g_push_n; i++) {
        Vec3 d = vec3_sub(g_push[i].a, a);
        if (sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) < 0.001f) {
            g_push[i].a = a; g_push[i].b = b; g_push[i].r = radius; g_push[i].t = ahora;
            return g_push_hit[i];
        }
    }
    if (g_push_n < MAX_PUSH) {
        g_push[g_push_n].a = a;
        g_push[g_push_n].b = b;
        g_push[g_push_n].r = radius;
        g_push[g_push_n].t = ahora;
        g_push_hit[g_push_n] = 0;
        g_push_n++;
    }
    return 0;
}

int g3d_cloth_push(float x, float y, float z, float radius) {
    return g3d_cloth_push_capsule(x, y, z, x, y, z, radius);   // una esfera
}

/* Aparta un punto de todos los empujones de este frame. Devuelve 1 si lo movio.
   Lo usan las telas y las CUERDAS: el mismo barril que abre una cortina tiene que
   apartar una cuerda tendida, y no tiene sentido tener dos listas. */
int g3d_push_apply(float *x, float *y, float *z) {
    int movido = 0;
    for (int e = 0; e < g_push_n; e++) {
        Vec3 ab = vec3_sub(g_push[e].b, g_push[e].a);
        float ab2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
        Vec3 p = vec3_make(*x, *y, *z);
        Vec3 cen = g_push[e].a;
        if (ab2 > 1e-8f) {
            Vec3 ap = vec3_sub(p, g_push[e].a);
            float t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / ab2;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            cen = vec3_add(g_push[e].a, vec3_scale(ab, t));
        }
        Vec3 d = vec3_sub(p, cen);
        float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < g_push[e].r && len > 1e-5f) {
            Vec3 np2 = vec3_add(cen, vec3_scale(d, g_push[e].r / len));
            *x = np2.x; *y = np2.y; *z = np2.z;
            g_push_hit[e]++;
            movido = 1;
        }
    }
    return movido;
}

/* Se quedan los de este frame (60 ms de margen); los demas se caen de la lista. */
void g3d_push_caducar(void) {
    Uint32 ahora = SDL_GetTicks();
    int j = 0;
    for (int i = 0; i < g_push_n; i++)
        if (ahora - g_push[i].t <= 60) { g_push_hit[j] = g_push_hit[i]; g_push[j++] = g_push[i]; }
    g_push_n = j;
}

static inline int IDX(Cloth *c, int i, int j) { return i + j * c->nx; }

int g3d_cloth_create(float width, float height, int nx, int ny,
                     float px, float py, float pz) {
    if (nx < 2) nx = 2; if (ny < 2) ny = 2;
    int id = -1;
    for (int k = 0; k < MAX_CLOTHS; k++) if (!g_cloths[k].active) { id = k; break; }
    if (id < 0) return -1;
    Cloth *c = &g_cloths[id];
    memset(c, 0, sizeof(*c));
    c->nx = nx; c->ny = ny;
    c->dx = width / (nx - 1); c->dy = height / (ny - 1);
    c->diag = sqrtf(c->dx * c->dx + c->dy * c->dy);
    c->gravity = 9.8f; c->damp = 0.99f;

    int np = nx * ny;
    c->pos = (Vec3 *)malloc(np * sizeof(Vec3));
    c->prev = (Vec3 *)malloc(np * sizeof(Vec3));
    c->pinPos = (Vec3 *)malloc(np * sizeof(Vec3));
    c->pinned = (unsigned char *)calloc(np, 1);
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            Vec3 p = vec3_make(px + i * c->dx, py - j * c->dy, pz);
            int k = IDX(c, i, j);
            c->pos[k] = c->prev[k] = c->pinPos[k] = p;
        }

    /* Build the grid mesh (2 tris per quad) */
    G3DVertex *verts = (G3DVertex *)calloc(np, sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)(nx - 1) * (ny - 1) * 6 * sizeof(uint32_t));
    int ic = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            int k = IDX(c, i, j);
            verts[k].position[0] = c->pos[k].x;
            verts[k].position[1] = c->pos[k].y;
            verts[k].position[2] = c->pos[k].z;
            verts[k].normal[2] = 1.0f;
            verts[k].texcoord[0] = (float)i / (nx - 1);
            verts[k].texcoord[1] = (float)j / (ny - 1);
        }
    for (int j = 0; j < ny - 1; j++)
        for (int i = 0; i < nx - 1; i++) {
            int a = IDX(c, i, j), b = IDX(c, i + 1, j);
            int d = IDX(c, i, j + 1), e = IDX(c, i + 1, j + 1);
            idx[ic++] = a; idx[ic++] = d; idx[ic++] = b;
            idx[ic++] = b; idx[ic++] = d; idx[ic++] = e;
        }
    c->mesh = g3d_mesh_create("cloth", verts, np, idx, ic);
    free(verts); free(idx);
    g3d_mesh_upload_gpu(c->mesh);

    c->material = g3d_material_impl_create();
    g3d_material_impl_set_color(c->material, 1, 1, 1, 1);

    int scene = g3d_scene_impl_get_active();
    c->entity = g3d_entity_impl_spawn(scene, 0, 0, 0, 0);
    G3DEntity *ent = g3d_entity_impl_get(c->entity);
    if (ent) { ent->mesh = c->mesh; ent->material_id = c->material; }

    c->active = 1;
    return id;
}

static Cloth *get(int id) {
    if (id < 0 || id >= MAX_CLOTHS || !g_cloths[id].active) return NULL;
    return &g_cloths[id];
}

void g3d_cloth_pin(int cloth, int mode) {
    Cloth *c = get(cloth); if (!c) return;
    memset(c->pinned, 0, c->nx * c->ny);
    if (mode == 0) {                 /* top edge */
        for (int i = 0; i < c->nx; i++) c->pinned[IDX(c, i, 0)] = 1;
    } else if (mode == 1) {          /* top corners */
        c->pinned[IDX(c, 0, 0)] = 1; c->pinned[IDX(c, c->nx - 1, 0)] = 1;
    } else {                         /* left edge (flag) */
        for (int j = 0; j < c->ny; j++) c->pinned[IDX(c, 0, j)] = 1;
    }
}

void g3d_cloth_set_wind(int cloth, float x, float y, float z, float strength) {
    Cloth *c = get(cloth); if (!c) return;
    c->wind = vec3_make(x, y, z); c->windStr = strength;
}
void g3d_cloth_set_collider(int cloth, float x, float y, float z, float radius) {
    Cloth *c = get(cloth); if (!c) return;
    c->colPos = vec3_make(x, y, z); c->colR = radius; c->hasCol = 1;
}
void g3d_cloth_pin_move(int cloth, int i, int j, float x, float y, float z) {
    Cloth *c = get(cloth); if (!c) return;
    if (i < 0 || j < 0 || i >= c->nx || j >= c->ny) return;
    int k = IDX(c, i, j);
    c->pinned[k] = 1;
    c->pinPos[k] = vec3_make(x, y, z);
}
void g3d_cloth_clear_collider(int cloth) { Cloth *c = get(cloth); if (c) c->hasCol = 0; }

void g3d_cloth_set_texture(int cloth, unsigned int gl_handle) {
    Cloth *c = get(cloth); if (!c) return;
    G3DMaterial *m = g3d_material_impl_get(c->material);
    if (!m) return;
    /* wrap the GL handle in a lightweight texture so render_mesh can bind it */
    static G3DTexture wrap[MAX_CLOTHS];
    wrap[cloth].gl_handle = gl_handle;
    wrap[cloth].gpu_uploaded = 1;
    m->albedo_texture = gl_handle ? &wrap[cloth] : NULL;
}

static void satisfy(Cloth *c, int a, int b, float rest) {
    Vec3 d = vec3_sub(c->pos[b], c->pos[a]);
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return;
    float f = (len - rest) / len;
    Vec3 corr = vec3_scale(d, 0.5f * f);
    int pa = c->pinned[a], pb = c->pinned[b];
    if (!pa && !pb) { c->pos[a] = vec3_add(c->pos[a], corr); c->pos[b] = vec3_sub(c->pos[b], corr); }
    else if (!pa && pb) c->pos[a] = vec3_add(c->pos[a], vec3_scale(d, f));
    else if (pa && !pb) c->pos[b] = vec3_sub(c->pos[b], vec3_scale(d, f));
}

void g3d_cloth_update(int cloth, float dt) {
    Cloth *c = get(cloth); if (!c) return;
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    if (dt > 0.05f) dt = 0.05f;
    float t = (float)SDL_GetTicks() / 1000.0f;
    int np = c->nx * c->ny;

    /* Verlet integration */
    for (int k = 0; k < np; k++) {
        if (c->pinned[k]) continue;
        Vec3 cur = c->pos[k];
        Vec3 vel = vec3_scale(vec3_sub(cur, c->prev[k]), c->damp);
        float flutter = 0.6f + 0.4f * sinf(t * 3.0f + cur.x * 0.6f + cur.y * 0.4f);
        Vec3 acc = vec3_make(c->wind.x * c->windStr * flutter,
                             -c->gravity + c->wind.y * c->windStr * flutter,
                             c->wind.z * c->windStr * flutter);
        Vec3 next = vec3_add(vec3_add(cur, vel), vec3_scale(acc, dt * dt));
        c->prev[k] = cur;
        c->pos[k] = next;
    }

    /* Constraint relaxation */
    for (int it = 0; it < ITERS; it++) {
        for (int j = 0; j < c->ny; j++)
            for (int i = 0; i < c->nx; i++) {
                int k = IDX(c, i, j);
                if (i < c->nx - 1) satisfy(c, k, IDX(c, i + 1, j), c->dx);
                if (j < c->ny - 1) satisfy(c, k, IDX(c, i, j + 1), c->dy);
                if (i < c->nx - 1 && j < c->ny - 1) {
                    satisfy(c, k, IDX(c, i + 1, j + 1), c->diag);
                    satisfy(c, IDX(c, i + 1, j), IDX(c, i, j + 1), c->diag);
                }
            }
        for (int k = 0; k < np; k++) if (c->pinned[k]) c->pos[k] = c->pinPos[k];
    }

    /* Sphere collision (the character) */
    /* los empujones de este frame: los aparta igual que el collider propio */
    g3d_push_caducar();
    for (int e = 0; e < g_push_n; e++) {
        int tocadas = 0;
        Vec3 ab = vec3_sub(g_push[e].b, g_push[e].a);
        float ab2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
        for (int k = 0; k < np; k++) {
            if (c->pinned[k]) continue;
            /* el punto de la capsula mas cercano a esta particula */
            Vec3 cen = g_push[e].a;
            if (ab2 > 1e-8f) {
                Vec3 ap = vec3_sub(c->pos[k], g_push[e].a);
                float t2 = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / ab2;
                if (t2 < 0.0f) t2 = 0.0f;
                if (t2 > 1.0f) t2 = 1.0f;
                cen = vec3_add(g_push[e].a, vec3_scale(ab, t2));
            }
            Vec3 d = vec3_sub(c->pos[k], cen);
            float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < g_push[e].r && len > 1e-5f) {
                c->pos[k] = vec3_add(cen, vec3_scale(d, g_push[e].r / len));
                tocadas++;
            }
        }
        g_push_hit[e] = tocadas;   // lo lee quien empuja, para saber que toca tela
    }
    if (c->hasCol) {
        for (int k = 0; k < np; k++) {
            if (c->pinned[k]) continue;
            Vec3 d = vec3_sub(c->pos[k], c->colPos);
            float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < c->colR && len > 1e-5f)
                c->pos[k] = vec3_add(c->colPos, vec3_scale(d, c->colR / len));
        }
    }

    /* Rebuild vertex positions + normals, re-upload */
    for (int j = 0; j < c->ny; j++)
        for (int i = 0; i < c->nx; i++) {
            int k = IDX(c, i, j);
            G3DVertex *v = &c->mesh->vertices[k];
            v->position[0] = c->pos[k].x; v->position[1] = c->pos[k].y; v->position[2] = c->pos[k].z;
            int il = i > 0 ? i - 1 : i, ir = i < c->nx - 1 ? i + 1 : i;
            int ju = j > 0 ? j - 1 : j, jd = j < c->ny - 1 ? j + 1 : j;
            Vec3 tx = vec3_sub(c->pos[IDX(c, ir, j)], c->pos[IDX(c, il, j)]);
            Vec3 ty = vec3_sub(c->pos[IDX(c, i, jd)], c->pos[IDX(c, i, ju)]);
            Vec3 n = vec3_cross(ty, tx);
            n = vec3_normalize(n);
            v->normal[0] = n.x; v->normal[1] = n.y; v->normal[2] = n.z;
        }
    g3d_mesh_calculate_bounds(c->mesh);
    g3d_mesh_update_gpu(c->mesh);
}

void g3d_cloth_shutdown(void) {
    for (int k = 0; k < MAX_CLOTHS; k++) {
        Cloth *c = &g_cloths[k];
        if (!c->active) continue;
        free(c->pos); free(c->prev); free(c->pinPos); free(c->pinned);
        c->active = 0;
    }
}
