/*
 * libmod_3d_pick.c - Screen-to-world picking
 */

#include "libmod_3d_pick.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_chunkterrain.h"
#include "libmod_3d_collide.h"
#include "libmod_3d_math.h"
#include <math.h>

static float g_px = 0.0f, g_py = 0.0f, g_pz = 0.0f;

/* Build a world-space ray (origin + normalized dir) from a screen pixel. */
static int screen_ray(G3DCamera *camera, float sx, float sy, float w, float h,
                      Vec3 *out_o, Vec3 *out_d) {
    if (!camera || w < 1.0f || h < 1.0f) return 0;
    Mat4 vp = mat4_multiply(g3d_camera_get_projection(camera),
                            g3d_camera_get_view(camera));
    Mat4 inv = mat4_inverse(vp);
    float ndcx = 2.0f * sx / w - 1.0f;
    float ndcy = 1.0f - 2.0f * sy / h;
    Vec4 pn = mat4_mul_vec4(inv, vec4_make(ndcx, ndcy, -1.0f, 1.0f));
    Vec4 pf = mat4_mul_vec4(inv, vec4_make(ndcx, ndcy,  1.0f, 1.0f));
    if (fabsf(pn.w) < 1e-6f || fabsf(pf.w) < 1e-6f) return 0;
    Vec3 a = vec3_make(pn.x / pn.w, pn.y / pn.w, pn.z / pn.w);
    Vec3 b = vec3_make(pf.x / pf.w, pf.y / pf.w, pf.z / pf.w);
    *out_o = camera->position;
    *out_d = vec3_normalize(vec3_sub(b, a));
    return 1;
}

/* Cast the screen ray against solid-collider entities; returns the hit entity
   id (or -1). Hit point readable via g3d_pick_x/y/z(). */
int g3d_pick_entity(G3DCamera *camera, float sx, float sy, float w, float h) {
    Vec3 o, d;
    if (!screen_ray(camera, sx, sy, w, h, &o, &d)) return -1;
    float dist = g3d_collide_raycast(o.x, o.y, o.z, d.x, d.y, d.z, 5000.0f);
    if (dist < 0.0f) return -1;
    g_px = g3d_collide_hit_x(); g_py = g3d_collide_hit_y(); g_pz = g3d_collide_hit_z();
    return g3d_collide_hit_entity();
}

int g3d_pick_terrain(G3DCamera *camera, float sx, float sy, float w, float h,
                     G3DMesh *terrain) {
    Vec3 o, dir;
    if (!screen_ray(camera, sx, sy, w, h, &o, &dir)) return 0;

    /* March range/step scale with the terrain size: a kilometric terrain is
       viewed from far away, so a fixed 2000u reach would never hit it. */
    float ws = (terrain && terrain->terrain_world_size > 0.0f)
                   ? terrain->terrain_world_size : 2000.0f;
    float maxd = ws * 3.0f + 2000.0f;
    float step = ws / 1500.0f;
    if (step < 0.25f) step = 0.25f;

    float prevt = 0.0f;
    for (float t = step; t < maxd; t += step) {
        float x = o.x + dir.x * t;
        float y = o.y + dir.y * t;
        float z = o.z + dir.z * t;
        float surf = terrain ? g3d_terrain_get_height(terrain, x, z) : 0.0f;
        if (y <= surf) {
            /* Bisect between the last "above" sample and this "below" one so the
               hit point is accurate regardless of the coarse step size. */
            float lo = prevt, hi = t;
            for (int it = 0; it < 24; it++) {
                float mid = 0.5f * (lo + hi);
                float mx = o.x + dir.x * mid, my = o.y + dir.y * mid, mz = o.z + dir.z * mid;
                float ms = terrain ? g3d_terrain_get_height(terrain, mx, mz) : 0.0f;
                if (my <= ms) hi = mid; else lo = mid;
            }
            g_px = o.x + dir.x * hi;
            g_pz = o.z + dir.z * hi;
            g_py = terrain ? g3d_terrain_get_height(terrain, g_px, g_pz) : 0.0f;
            return 1;
        }
        prevt = t;
    }
    return 0;
}

/* Like g3d_pick_terrain but samples a raw shared height array (for chunked
   terrains, which have no single mesh). side*side, spanning world_size. */
int g3d_pick_heightfield(G3DCamera *camera, float sx, float sy, float w, float h,
                         const float *H, int side, float world_size) {
    Vec3 o, dir;
    if (!screen_ray(camera, sx, sy, w, h, &o, &dir)) return 0;

    float ws = world_size > 0.0f ? world_size : 2000.0f;
    float maxd = ws * 3.0f + 2000.0f;
    float step = ws / 1500.0f;
    if (step < 0.25f) step = 0.25f;

    float prevt = 0.0f;
    for (float t = step; t < maxd; t += step) {
        float x = o.x + dir.x * t, y = o.y + dir.y * t, z = o.z + dir.z * t;
        float surf = g3d_heightfield_height(H, side, ws, x, z);
        if (y <= surf) {
            float lo = prevt, hi = t;
            for (int it = 0; it < 24; it++) {
                float mid = 0.5f * (lo + hi);
                float my = o.y + dir.y * mid;
                float ms = g3d_heightfield_height(H, side, ws,
                                                  o.x + dir.x * mid, o.z + dir.z * mid);
                if (my <= ms) hi = mid; else lo = mid;
            }
            g_px = o.x + dir.x * hi;
            g_pz = o.z + dir.z * hi;
            g_py = g3d_heightfield_height(H, side, ws, g_px, g_pz);
            return 1;
        }
        prevt = t;
    }
    return 0;
}

float g3d_pick_x(void) { return g_px; }
float g3d_pick_y(void) { return g_py; }
float g3d_pick_z(void) { return g_pz; }
