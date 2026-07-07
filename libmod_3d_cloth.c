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
