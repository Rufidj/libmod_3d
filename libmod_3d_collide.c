/*
 * libmod_3d_collide.c - Collision queries against solid entity AABBs
 */

#include "libmod_3d_collide.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_math.h"
#include <math.h>

static float g_res_x = 0.0f, g_res_z = 0.0f;
static float g_hit_x = 0.0f, g_hit_y = 0.0f, g_hit_z = 0.0f;
static int g_hit_entity = -1;

/* World-space AABB of a solid collider entity. Returns 0 if not a collider. */
static int entity_world_aabb(int eid, float *mn, float *mx) {
    G3DEntity *e = g3d_entity_impl_get(eid);
    if (!e || !e->active || !e->collider || !e->mesh)
        return 0;
    G3DMesh *m = (G3DMesh *)e->mesh;
    Mat4 w = g3d_entity_impl_get_world_matrix(eid);
    int first = 1;
    for (int c = 0; c < 8; c++) {
        Vec3 corner = vec3_make((c & 1) ? m->aabb_max[0] : m->aabb_min[0],
                                (c & 2) ? m->aabb_max[1] : m->aabb_min[1],
                                (c & 4) ? m->aabb_max[2] : m->aabb_min[2]);
        Vec3 p = mat4_transform_point(w, corner);
        float pp[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; k++) {
            if (first || pp[k] < mn[k]) mn[k] = pp[k];
            if (first || pp[k] > mx[k]) mx[k] = pp[k];
        }
        first = 0;
    }
    return 1;
}

int g3d_collide_move_character(float x, float y, float z, float radius,
                               float height, float dx, float dz) {
    float nx = x + dx;
    float nz = z + dz;

    int count = 0;
    int *ents = g3d_scene_impl_get_entities(&count);

    /* Resolve X, then Z (axis-separated) so movement slides along walls. */
    for (int i = 0; i < count; i++) {
        float mn[3], mx[3];
        if (!entity_world_aabb(ents[i], mn, mx))
            continue;
        if (y + height < mn[1] || y > mx[1] - 0.2f)
            continue; /* above it, or standing on top -> not a wall */
        float eminx = mn[0] - radius, emaxx = mx[0] + radius;
        float eminz = mn[2] - radius, emaxz = mx[2] + radius;
        if (z > eminz && z < emaxz && nx > eminx && nx < emaxx) {
            if (dx > 0.0f) nx = eminx;
            else if (dx < 0.0f) nx = emaxx;
        }
    }
    for (int i = 0; i < count; i++) {
        float mn[3], mx[3];
        if (!entity_world_aabb(ents[i], mn, mx))
            continue;
        if (y + height < mn[1] || y > mx[1] - 0.2f)
            continue;
        float eminx = mn[0] - radius, emaxx = mx[0] + radius;
        float eminz = mn[2] - radius, emaxz = mx[2] + radius;
        if (nx > eminx && nx < emaxx && nz > eminz && nz < emaxz) {
            if (dz > 0.0f) nz = eminz;
            else if (dz < 0.0f) nz = emaxz;
        }
    }

    g_res_x = nx;
    g_res_z = nz;
    return 1;
}

float g3d_collide_result_x(void) { return g_res_x; }
float g3d_collide_result_z(void) { return g_res_z; }

float g3d_collide_floor(float x, float z, float radius, float feet_y) {
    const float STEP = 1.0f;   /* auto step-up tolerance */
    int count = 0;
    int *ents = g3d_scene_impl_get_entities(&count);
    float best = -1e9f;
    for (int i = 0; i < count; i++) {
        float mn[3], mx[3];
        if (!entity_world_aabb(ents[i], mn, mx))
            continue;
        if (x > mn[0] - radius && x < mx[0] + radius &&
            z > mn[2] - radius && z < mx[2] + radius) {
            float top = mx[1];
            if (top <= feet_y + STEP && top > best)
                best = top;
        }
    }
    return best;
}

float g3d_collide_raycast(float ox, float oy, float oz,
                          float dx, float dy, float dz, float maxdist) {
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-8f) { g_hit_entity = -1; return -1.0f; }
    float o[3] = {ox, oy, oz};
    float d[3] = {dx / len, dy / len, dz / len};

    int count = 0;
    int *ents = g3d_scene_impl_get_entities(&count);
    float best = maxdist;
    int best_e = -1;

    for (int i = 0; i < count; i++) {
        float mn[3], mx[3];
        if (!entity_world_aabb(ents[i], mn, mx))
            continue;
        float t0 = 0.0f, t1 = maxdist;
        int ok = 1;
        for (int a = 0; a < 3; a++) {
            if (fabsf(d[a]) < 1e-8f) {
                if (o[a] < mn[a] || o[a] > mx[a]) { ok = 0; break; }
            } else {
                float inv = 1.0f / d[a];
                float ta = (mn[a] - o[a]) * inv;
                float tb = (mx[a] - o[a]) * inv;
                if (ta > tb) { float s = ta; ta = tb; tb = s; }
                if (ta > t0) t0 = ta;
                if (tb < t1) t1 = tb;
                if (t0 > t1) { ok = 0; break; }
            }
        }
        if (ok && t0 >= 0.0f && t0 < best) {
            best = t0;
            best_e = ents[i];
        }
    }

    if (best_e < 0) { g_hit_entity = -1; return -1.0f; }
    g_hit_x = o[0] + d[0] * best;
    g_hit_y = o[1] + d[1] * best;
    g_hit_z = o[2] + d[2] * best;
    g_hit_entity = best_e;
    return best;
}

float g3d_collide_hit_x(void) { return g_hit_x; }
float g3d_collide_hit_y(void) { return g_hit_y; }
float g3d_collide_hit_z(void) { return g_hit_z; }
int g3d_collide_hit_entity(void) { return g_hit_entity; }
