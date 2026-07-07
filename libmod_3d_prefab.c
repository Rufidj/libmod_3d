/*
 * libmod_3d_prefab.c - Prefabs: composable groups of pieces
 */

#include "libmod_3d_prefab.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_material.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PREFABS 64
#define DEG2RAD 0.01745329252f

typedef struct {
    float pos[3], rot[3], scale[3];   /* rot in degrees */
    float color[3];
    char texture[160];                /* "" = none */
    int collider;
} Piece;

typedef struct {
    int active;
    char name[64];
    Piece *pieces;
    int count, cap;
} Prefab;

static Prefab g_pf[MAX_PREFABS];

/* Shared unit cube + a tiny texture cache (by path). */
static G3DMesh *s_cube = NULL;
#define TEX_CACHE 64
static struct { char path[160]; void *tex; } s_texcache[TEX_CACHE];
static int s_texcount = 0;

static G3DMesh *cube_mesh(void) {
    if (!s_cube) {
        s_cube = g3d_primitive_create_cube();
        if (s_cube) g3d_mesh_upload_gpu(s_cube);
    }
    return s_cube;
}

static void *load_texture_cached(const char *path) {
    if (!path || !path[0]) return NULL;
    for (int i = 0; i < s_texcount; i++)
        if (strcmp(s_texcache[i].path, path) == 0) return s_texcache[i].tex;
    G3DTexture *t = g3d_texture_load_impl(path);
    if (t) g3d_texture_upload_gpu(t);
    if (s_texcount < TEX_CACHE) {
        strncpy(s_texcache[s_texcount].path, path, sizeof(s_texcache[0].path) - 1);
        s_texcache[s_texcount].tex = t;
        s_texcount++;
    }
    return t;
}

static Prefab *get(int id) {
    if (id < 0 || id >= MAX_PREFABS || !g_pf[id].active) return NULL;
    return &g_pf[id];
}

int g3d_prefab_create(const char *name) {
    int idx = -1;
    for (int i = 0; i < MAX_PREFABS; i++) if (!g_pf[i].active) { idx = i; break; }
    if (idx < 0) return -1;
    Prefab *p = &g_pf[idx];
    memset(p, 0, sizeof(*p));
    p->active = 1;
    if (name) strncpy(p->name, name, sizeof(p->name) - 1);
    p->cap = 8;
    p->pieces = (Piece *)calloc(p->cap, sizeof(Piece));
    return idx;
}

int g3d_prefab_add_box(int prefab, float px, float py, float pz,
                       float sx, float sy, float sz,
                       float rx, float ry, float rz,
                       float r, float g, float b, int collider) {
    Prefab *p = get(prefab);
    if (!p) return -1;
    if (p->count >= p->cap) {
        p->cap *= 2;
        p->pieces = (Piece *)realloc(p->pieces, p->cap * sizeof(Piece));
    }
    Piece *pc = &p->pieces[p->count];
    memset(pc, 0, sizeof(*pc));
    pc->pos[0] = px; pc->pos[1] = py; pc->pos[2] = pz;
    pc->scale[0] = sx; pc->scale[1] = sy; pc->scale[2] = sz;
    pc->rot[0] = rx; pc->rot[1] = ry; pc->rot[2] = rz;
    pc->color[0] = r; pc->color[1] = g; pc->color[2] = b;
    pc->collider = collider ? 1 : 0;
    p->count++;
    return p->count - 1;
}

void g3d_prefab_piece_texture(int prefab, const char *path) {
    Prefab *p = get(prefab);
    if (!p || p->count == 0 || !path) return;
    strncpy(p->pieces[p->count - 1].texture, path,
            sizeof(p->pieces[0].texture) - 1);
}

int g3d_prefab_count(int prefab) {
    Prefab *p = get(prefab);
    return p ? p->count : 0;
}

int g3d_prefab_save(int prefab, const char *file) {
    Prefab *p = get(prefab);
    if (!p || !file) return 0;
    FILE *f = fopen(file, "w");
    if (!f) return 0;
    fprintf(f, "PREFAB %s\n", p->name);
    for (int i = 0; i < p->count; i++) {
        Piece *pc = &p->pieces[i];
        fprintf(f, "BOX %g %g %g %g %g %g %g %g %g %g %g %g %d %s\n",
                pc->pos[0], pc->pos[1], pc->pos[2],
                pc->scale[0], pc->scale[1], pc->scale[2],
                pc->rot[0], pc->rot[1], pc->rot[2],
                pc->color[0], pc->color[1], pc->color[2],
                pc->collider, pc->texture[0] ? pc->texture : "_");
    }
    fprintf(f, "END\n");
    fclose(f);
    return 1;
}

int g3d_prefab_load(const char *file) {
    if (!file) return -1;
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    char line[512];
    char name[64] = "loaded";
    int id = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PREFAB", 6) == 0) {
            sscanf(line, "PREFAB %63s", name);
            id = g3d_prefab_create(name);
        } else if (strncmp(line, "BOX", 3) == 0 && id >= 0) {
            Piece pc; memset(&pc, 0, sizeof(pc));
            char tex[160] = "_";
            sscanf(line, "BOX %f %f %f %f %f %f %f %f %f %f %f %f %d %159s",
                   &pc.pos[0], &pc.pos[1], &pc.pos[2],
                   &pc.scale[0], &pc.scale[1], &pc.scale[2],
                   &pc.rot[0], &pc.rot[1], &pc.rot[2],
                   &pc.color[0], &pc.color[1], &pc.color[2],
                   &pc.collider, tex);
            int pi = g3d_prefab_add_box(id, pc.pos[0], pc.pos[1], pc.pos[2],
                                        pc.scale[0], pc.scale[1], pc.scale[2],
                                        pc.rot[0], pc.rot[1], pc.rot[2],
                                        pc.color[0], pc.color[1], pc.color[2],
                                        pc.collider);
            if (pi >= 0 && strcmp(tex, "_") != 0)
                g3d_prefab_piece_texture(id, tex);
        }
    }
    fclose(f);
    return id;
}

int g3d_prefab_instantiate(int prefab, int scene, float x, float y, float z,
                           float yaw_deg) {
    Prefab *p = get(prefab);
    if (!p) return 0;
    G3DMesh *cube = cube_mesh();
    if (!cube) return 0;

    Quat placeQ = quat_from_euler(0.0f, yaw_deg * DEG2RAD, 0.0f);
    Vec3 placeP = vec3_make(x, y, z);
    int spawned = 0;

    for (int i = 0; i < p->count; i++) {
        Piece *pc = &p->pieces[i];

        /* world transform = placement * local */
        Vec3 lp = vec3_make(pc->pos[0], pc->pos[1], pc->pos[2]);
        Vec3 wp = vec3_add(placeP, quat_rotate_vec3(placeQ, lp));
        Quat pieceQ = quat_from_euler(pc->rot[0] * DEG2RAD, pc->rot[1] * DEG2RAD,
                                      pc->rot[2] * DEG2RAD);
        Quat wq = quat_multiply(placeQ, pieceQ);

        int ent = g3d_entity_impl_spawn(scene, 0, wp.x, wp.y, wp.z);
        G3DEntity *e = g3d_entity_impl_get(ent);
        if (!e) continue;
        e->mesh = cube;
        e->rotation = wq;
        e->world_matrix_dirty = 1;
        g3d_entity_impl_set_scale(ent, pc->scale[0], pc->scale[1], pc->scale[2]);

        int mat = g3d_material_impl_create();
        g3d_material_impl_set_color(mat, pc->color[0], pc->color[1], pc->color[2], 1.0f);
        if (pc->texture[0]) {
            void *t = load_texture_cached(pc->texture);
            G3DMaterial *m = g3d_material_impl_get(mat);
            if (m && t) m->albedo_texture = t;
        }
        e->material_id = mat;
        e->collider = pc->collider;
        spawned++;
    }
    return spawned;
}

void g3d_prefab_shutdown(void) {
    for (int i = 0; i < MAX_PREFABS; i++) {
        if (g_pf[i].active) { free(g_pf[i].pieces); g_pf[i].active = 0; }
    }
    s_texcount = 0;
}
