/*
 * libmod_3d_entity.c - Entity Management Implementation
 */

#include "libmod_3d_entity.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_material.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_sprite.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G3D_MAX_ENTITIES 16384
static G3DEntity g_entities[G3D_MAX_ENTITIES];
static int g_entity_count = 0;

/* Todo lo que se cree mientras hay un 'padre de creacion' cuelga de el: asi un mapa 2.5D se construye con SUS coordenadas y se coloca
   en el mundo moviendo una entidad raiz (los sprites, que no son entidades, reciben su posicion como origen). -1 = nada. */
static int g_spawn_parent = -1;
int g3d_entity_impl_spawn(int scene_id, int model_id, float x, float y, float z) {
    /* Reuse a freed slot first (long-running games spawn/despawn constantly:
       projectiles, fracture chunks, particles). */
    int entity_id = -1;
    for (int i = 0; i < g_entity_count; i++) {
        if (!g_entities[i].active) { entity_id = i; break; }
    }
    if (entity_id < 0) {
        if (g_entity_count >= G3D_MAX_ENTITIES) {
            fprintf(stderr, "G3D: Max entities reached\n");
            return -1;
        }
        entity_id = g_entity_count++;
    }
    G3DEntity *entity = &g_entities[entity_id];

    memset(entity, 0, sizeof(G3DEntity));
    entity->id = entity_id;
    entity->scene_id = scene_id;
    entity->model_id = model_id;
    entity->material_id = -1;
    entity->parent_id = (g_spawn_parent >= 0 && g_spawn_parent != entity_id) ? g_spawn_parent : -1;
    entity->active = 1;
    entity->world_matrix_dirty = 1;
    entity->occ_visible = 1;  /* drawn until a query proves it's hidden */
    entity->opacity = 1.0f;   /* opaque by default */
    entity->tint[0] = entity->tint[1] = entity->tint[2] = 1.0f;   /* no tint */
    entity->blend_mode = G3D_BLEND_NORMAL;
    /* default custom factors = standard alpha (GL enums) */
    entity->blend_custom[0] = 0x0302; entity->blend_custom[1] = 0x0303;  /* SRC_ALPHA, ONE_MINUS_SRC_ALPHA */
    entity->blend_custom[2] = 0x0302; entity->blend_custom[3] = 0x0303;
    entity->blend_custom[4] = 0x8006; entity->blend_custom[5] = 0x8006;  /* FUNC_ADD */

    /* Default transform */
    entity->position = vec3_make(x, y, z);
    entity->rotation = quat_identity();
    entity->scale = vec3_make(1, 1, 1);

    /* Add to scene */
    g3d_scene_impl_add_entity(scene_id, entity_id);

    snprintf(entity->name, 63, "Entity_%d", entity_id);

    return entity_id;
}

int g3d_entity_impl_destroy(int entity_id) {
    if (entity_id < 0 || entity_id >= g_entity_count)
        return 0;

    G3DEntity *entity = &g_entities[entity_id];

    if (!entity->active)
        return 0;

    /* Remove from scene */
    g3d_scene_impl_remove_entity(entity->scene_id, entity_id);

    entity->active = 0;
    return 1;
}

/* Floating origin: shift every root entity (children move with their parent) by
   (-dx,-dy,-dz). The game calls this when the camera drifts far from the origin
   so world coordinates stay small (float precision). Physics is rebased
   separately (g3d_world_rebase). */
void g3d_entity_impl_rebase(float dx, float dy, float dz) {
    for (int i = 0; i < g_entity_count; i++) {
        G3DEntity *e = &g_entities[i];
        if (!e->active || e->parent_id >= 0) continue;   /* children follow roots */
        e->position.x -= dx; e->position.y -= dy; e->position.z -= dz;
        e->world_matrix_dirty = 1;
    }
}

/* Destroy an entity and everything parented under it (recursively). If
   free_materials != 0 each destroyed entity's material is also released - use
   this for models spawned by g3d_model_spawn, whose per-submesh materials are
   private to that spawn. */
int g3d_entity_impl_destroy_tree(int root_id, int free_materials) {
    if (root_id < 0 || root_id >= g_entity_count)
        return 0;
    if (!g_entities[root_id].active)
        return 0;

    /* Children first (scan the whole live range; parenting is shallow here). */
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].active && g_entities[i].parent_id == root_id)
            g3d_entity_impl_destroy_tree(i, free_materials);
    }

    G3DEntity *entity = &g_entities[root_id];
    if (free_materials && entity->material_id >= 0)
        g3d_material_impl_destroy(entity->material_id);
    g3d_scene_impl_remove_entity(entity->scene_id, root_id);
    entity->active = 0;
    return 1;
}

G3DEntity *g3d_entity_impl_get(int entity_id) {
    if (entity_id < 0 || entity_id >= g_entity_count)
        return NULL;

    G3DEntity *entity = &g_entities[entity_id];
    if (!entity->active)
        return NULL;

    return entity;
}

int g3d_entity_impl_set_position(int entity_id, float x, float y, float z) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->position = vec3_make(x, y, z);
    entity->world_matrix_dirty = 1;
    return 1;
}

/* Rotacion actual de la entidad en angulos de Euler (radianes), la inversa de
   g3d_entity_impl_set_rotation. Permite a un anfitrion (o al main de un juego)
   saber hacia donde mira un objeto sin tener que llevar la cuenta por su lado:
   util, por ejemplo, para colgar un arma del hueso de un personaje al que mueve
   otro codigo. */
int g3d_entity_impl_get_rotation(int entity_id, float *pitch, float *yaw, float *roll) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;
    float p, y, r;
    quat_to_euler(entity->rotation, &p, &y, &r);
    if (pitch) *pitch = p;
    if (yaw)   *yaw   = y;
    if (roll)  *roll  = r;
    return 1;
}

int g3d_entity_impl_set_rotation(int entity_id, float pitch, float yaw, float roll) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->rotation = quat_from_euler(pitch, yaw, roll);
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_set_collider(int entity_id, int solid) {
    G3DEntity *e = g3d_entity_impl_get(entity_id);
    if (!e) return 0;
    e->collider = solid ? 1 : 0;
    return 1;
}

int g3d_entity_impl_set_scale(int entity_id, float sx, float sy, float sz) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->scale = vec3_make(sx, sy, sz);
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_get_position(int entity_id, float *x, float *y, float *z) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    if (x)
        *x = entity->position.x;
    if (y)
        *y = entity->position.y;
    if (z)
        *z = entity->position.z;

    return 1;
}

int g3d_entity_impl_set_parent(int entity_id, int parent_id) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->parent_id = parent_id;
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_set_material(int entity_id, int material_id) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->material_id = material_id;
    return 1;
}

int g3d_entity_impl_set_alpha(int entity_id, float opacity) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;
    if (opacity < 0.0f) opacity = 0.0f; else if (opacity > 1.0f) opacity = 1.0f;
    entity->opacity = opacity;
    /* Apply to the whole tree so it works on a g3d_model_spawn root (empty root
       + submesh children). Parenting here is shallow (root -> children). */
    for (int i = 0; i < g_entity_count; i++)
        if (g_entities[i].active && g_entities[i].parent_id == entity_id)
            g3d_entity_impl_set_alpha(i, opacity);
    return 1;
}

int g3d_entity_impl_set_color(int entity_id, float r, float g, float b) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;
    if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;
    entity->tint[0] = r; entity->tint[1] = g; entity->tint[2] = b;
    for (int i = 0; i < g_entity_count; i++)
        if (g_entities[i].active && g_entities[i].parent_id == entity_id)
            g3d_entity_impl_set_color(i, r, g, b);
    return 1;
}

int g3d_entity_impl_set_blend(int entity_id, int mode) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;
    entity->blend_mode = mode;
    for (int i = 0; i < g_entity_count; i++)
        if (g_entities[i].active && g_entities[i].parent_id == entity_id)
            g3d_entity_impl_set_blend(i, mode);
    return 1;
}

int g3d_entity_impl_set_blend_custom(int entity_id, int src_rgb, int dst_rgb,
                                     int src_alpha, int dst_alpha,
                                     int eq_rgb, int eq_alpha) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;
    entity->blend_custom[0] = src_rgb;   entity->blend_custom[1] = dst_rgb;
    entity->blend_custom[2] = src_alpha; entity->blend_custom[3] = dst_alpha;
    entity->blend_custom[4] = eq_rgb;    entity->blend_custom[5] = eq_alpha;
    for (int i = 0; i < g_entity_count; i++)
        if (g_entities[i].active && g_entities[i].parent_id == entity_id)
            g3d_entity_impl_set_blend_custom(i, src_rgb, dst_rgb, src_alpha, dst_alpha, eq_rgb, eq_alpha);
    return 1;
}

Mat4 g3d_entity_impl_get_world_matrix(int entity_id) {
    static Mat4 identity;
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return mat4_identity();

    /* Parented entities must recompute every frame: a moving parent does not mark
       its children dirty, so the cache is only safe for root (unparented) nodes. */
    if (!entity->world_matrix_dirty && entity->parent_id < 0)
        return entity->world_matrix;

    /* Build TRS matrix */
    Mat4 world = mat4_trs(entity->position, entity->rotation, entity->scale);

    /* Apply parent transform if exists */
    if (entity->parent_id >= 0) {
        Mat4 parent_world = g3d_entity_impl_get_world_matrix(entity->parent_id);
        world = mat4_multiply(parent_world, world);
    }

    entity->world_matrix = world;
    entity->world_matrix_dirty = 0;

    return world;
}

void g3d_entity_impl_shutdown(void) {
    for (int i = 0; i < g_entity_count; i++) {
        g_entities[i].active = 0;
    }
    g_entity_count = 0;
    printf("G3D: Entity system shut down\n");
}


/* ---------------------------------------------------------------------------
 *  X-ray: muros (o lo que sea) que se vuelven translucidos al taparle la camara al personaje
 * ------------------------------------------------------------------------- */
int g3d_entity_impl_set_xray(int entity_id, int on) {
    G3DEntity *e = g3d_entity_impl_get(entity_id);
    if (!e) return 0;
    if (on && !e->xray) { e->xray_base = e->opacity; e->xray_fade = 1.0f; }
    if (!on && e->xray) e->opacity = e->xray_base;
    e->xray = on ? 1 : 0;
    return 1;
}

/* el segmento a->b (t en 0..1) corta la caja [mn,mx]? (metodo de las losas) */
static int seg_hits_box(const float *a, const float *b, const float *mn, const float *mx) {
    float t0 = 0.0f, t1 = 1.0f;
    for (int k = 0; k < 3; k++) {
        float d = b[k] - a[k];
        if (fabsf(d) < 1e-6f) {
            if (a[k] < mn[k] || a[k] > mx[k]) return 0;
        } else {
            float ta = (mn[k] - a[k]) / d, tb = (mx[k] - a[k]) / d;
            if (ta > tb) { float t = ta; ta = tb; tb = t; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) return 0;
        }
    }
    return 1;
}

void g3d_xray_update(float cx, float cy, float cz, float tx, float ty, float tz,
                     float radius, float min_alpha, float speed) {
    float a[3] = { cx, cy, cz }, b[3] = { tx, ty, tz };
    {   // el segmento acaba 'radius' antes del objetivo: un muro justo DELANTE del objetivo (que no tapa nada) no debe hacerse translucido
        float d[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
        float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        float cut = (len > radius * 2.0f) ? radius : len * 0.5f;
        if (len > 1e-4f) for (int k = 0; k < 3; k++) b[k] -= d[k] / len * cut;
        // y empieza 'radius' despues de la camara: un muro pegado a la camara (a su lado) tampoco tapa nada
        if (len > radius * 3.0f) for (int k = 0; k < 3; k++) a[k] += d[k] / len * radius;
    }
    if (min_alpha < 0.0f) min_alpha = 0.0f; else if (min_alpha > 1.0f) min_alpha = 1.0f;
    if (speed < 0.01f) speed = 0.01f; else if (speed > 1.0f) speed = 1.0f;
    for (int i = 0; i < g_entity_count; i++) {
        G3DEntity *e = &g_entities[i];
        if (!e->active || !e->xray || !e->mesh) continue;
        G3DMesh *m = (G3DMesh *)e->mesh;
        Mat4 w = g3d_entity_impl_get_world_matrix(i);
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (int c = 0; c < 8; c++) {
            Vec3 corner = vec3_make((c & 1) ? m->aabb_max[0] : m->aabb_min[0],
                                    (c & 2) ? m->aabb_max[1] : m->aabb_min[1],
                                    (c & 4) ? m->aabb_max[2] : m->aabb_min[2]);
            Vec3 p = mat4_transform_point(w, corner);
            float pp[3] = { p.x, p.y, p.z };
            for (int k = 0; k < 3; k++) { if (pp[k] < mn[k]) mn[k] = pp[k]; if (pp[k] > mx[k]) mx[k] = pp[k]; }
        }
        for (int k = 0; k < 3; k++) { mn[k] -= radius; mx[k] += radius; }
        float goal = seg_hits_box(a, b, mn, mx) ? min_alpha : 1.0f;
        e->xray_fade += (goal - e->xray_fade) * speed;
        if (fabsf(e->xray_fade - goal) < 0.004f) e->xray_fade = goal;
        e->opacity = (e->xray_fade >= 0.999f) ? e->xray_base : e->xray_base * e->xray_fade;
    }
}

void g3d_entity_impl_set_spawn_parent(int parent_id) {
    g_spawn_parent = parent_id;
    if (parent_id >= 0) {
        Mat4 w = g3d_entity_impl_get_world_matrix(parent_id);
        /* giro del grupo en Y a partir de la matriz del mundo (columna X = (cos, 0, -sin)) */
        g3d_sprite_set_origin(w.m[12], w.m[13], w.m[14], atan2f(-w.m[2], w.m[0]));
    } else {
        g3d_sprite_set_origin(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
