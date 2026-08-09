/*
 * libmod_3d_water_splash.c - Droplets where the water hits something
 *
 * See the header for the idea. Two sources, both read straight off the unified
 * field so nothing has to be authored:
 *
 *   - The foot of every waterfall. The falls module already knows where each
 *     curtain lands; taking the position from there rather than recomputing it
 *     means the spray can never drift off the water.
 *   - Solid colliders standing in moving water. A rock in a rapid throws water
 *     because the field says fast water is arriving at it; the same rock in a
 *     still lake does nothing.
 *
 * Everything is emitted as one-shot bursts rather than persistent emitters. A
 * river can appear, move or dry up at any moment, and persistent emitters would
 * need to be tracked and destroyed in step with it -- bursts simply stop being
 * created and the last ones fade out on their own.
 */

#include "libmod_3d_water_splash.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_falls.h"
#include "libmod_3d_particles.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SPL_MAX_FEET     64      /* falls sprayed at once */
#define SPL_MAX_TRACKED  128     /* rocks with their own spawn budget */

typedef struct {
    int   eid;
    float budget;                /* fractional droplets owed */
} SplTracked;

static struct {
    float amount;
    float threshold;             /* flow speed a rock needs before it splashes */
    float foot_budget[SPL_MAX_FEET];
    SplTracked tracked[SPL_MAX_TRACKED];
    int   ntracked;
    int   emitted;               /* bursts since the last debug read */
    float scan_accum;            /* time owed to the obstacle scan */
} SP = { .amount = 1.0f, .threshold = 0.8f };

/* World-space AABB of a solid collider. Deliberately a local copy of the one in
   libmod_3d_collide.c: the splashes are a renderer-side effect and must not
   drag the collision system into the build on platforms that skip it. */
/* La caja de UNA entidad con malla propia. */
static int spl_mesh_aabb(int eid, float *mn, float *mx, int *first_io) {
    G3DEntity *e = g3d_entity_impl_get(eid);
    if (!e || !e->active || !e->mesh) return 0;
    G3DMesh *m = (G3DMesh *)e->mesh;
    Mat4 w = g3d_entity_impl_get_world_matrix(eid);
    for (int c = 0; c < 8; c++) {
        Vec3 corner = vec3_make((c & 1) ? m->aabb_max[0] : m->aabb_min[0],
                                (c & 2) ? m->aabb_max[1] : m->aabb_min[1],
                                (c & 4) ? m->aabb_max[2] : m->aabb_min[2]);
        Vec3 p = mat4_transform_point(w, corner);
        float pp[3] = { p.x, p.y, p.z };
        for (int k = 0; k < 3; k++) {
            if (*first_io || pp[k] < mn[k]) mn[k] = pp[k];
            if (*first_io || pp[k] > mx[k]) mx[k] = pp[k];
        }
        *first_io = 0;
    }
    return 1;
}

/* La caja de lo que el usuario ve como UN objeto.
 *
 * Un modelo cargado no es una entidad: g3d_model_spawn crea un root VACIO -- sin
 * malla, no se dibuja -- y le cuelga una entidad por submalla. El editor marca
 * como solido ese root, que es el que te devuelve. Pero aqui se pedia
 * `e->collider && e->mesh` a la MISMA entidad, y eso no lo cumple nadie: el root
 * lleva la marca y no tiene malla, y los hijos tienen malla y no llevan marca.
 * Resultado medido en una escena de verdad: 148 entidades, 0 validas, y por
 * tanto ni el rio rodeaba las rocas, ni salpicaba en ellas, ni la cascada se
 * partia -- las tres cosas comen de esta lista.
 *
 * Asi que la marca se busca en el root y la geometria en sus hijos. */
static int spl_entity_aabb(int eid, float *mn, float *mx) {
    G3DEntity *e = g3d_entity_impl_get(eid);
    if (!e || !e->active) return 0;
    if (!e->collider) return 0;
    int first = 1;
    spl_mesh_aabb(eid, mn, mx, &first);      /* por si la lleva el mismo */
    int n = 0;
    int *all = g3d_scene_impl_get_entities(&n);
    for (int i = 0; i < n && all; i++) {
        G3DEntity *c = g3d_entity_impl_get(all[i]);
        if (c && c->active && c->mesh && c->parent_id == eid)
            spl_mesh_aabb(all[i], mn, mx, &first);
    }
    return !first;                            /* 1 si se junto alguna malla */
}

static float *spl_budget_for(int eid) {
    for (int i = 0; i < SP.ntracked; i++)
        if (SP.tracked[i].eid == eid) return &SP.tracked[i].budget;
    if (SP.ntracked >= SPL_MAX_TRACKED) return NULL;
    SP.tracked[SP.ntracked].eid = eid;
    SP.tracked[SP.ntracked].budget = 0.0f;
    return &SP.tracked[SP.ntracked++].budget;
}

/* Spend a fractional per-second rate without tying the effect to the frame
   rate: the leftover carries to the next frame instead of being rounded away,
   which at 144 fps would otherwise round almost everything to zero. */
static int spl_take(float *budget, float rate, float dt, int cap) {
    *budget += rate * dt;
    int n = (int)(*budget);
    if (n <= 0) return 0;
    if (n > cap) n = cap;
    *budget -= (float)n;
    if (*budget > 4.0f) *budget = 4.0f;     /* never bank a backlog */
    return n;
}

/* ---------------------------------------------------------------------------
   The foot of a waterfall
   --------------------------------------------------------------------------- */

static void spl_falls(float dt) {
    static float feet[SPL_MAX_FEET * 5];
    int n = g3d_water_falls_feet(feet, SPL_MAX_FEET);

    for (int i = 0; i < n; i++) {
        const float *f = &feet[i * 5];
        float x = f[0], y = f[1], z = f[2], drop = f[3], width = f[4];

        /* Water arriving from higher up hits harder and throws further. The
           speed it lands at is the speed it fell at -- sqrt(2gh) -- which is
           also what decides how high the spray bounces back up. */
        float impact = sqrtf(2.0f * 9.81f * (drop > 0.2f ? drop : 0.2f));

        float rate = 26.0f * SP.amount * (0.5f + 0.5f * (drop / 6.0f));
        if (rate > 70.0f) rate = 70.0f;
        int count = spl_take(&SP.foot_budget[i], rate, dt, 12);
        if (count <= 0) continue;

        /* Spawned a touch above the pool so the droplets are visibly leaving
           the water rather than climbing out of it. */
        g3d_particles_burst(x, y + 0.12f, z, count,
                            impact * 0.30f,
                            width * 0.22f,
                            0.55f + drop * 0.03f,
                            0.93f, 0.96f, 1.0f);
        SP.emitted++;
    }
    /* Falls that stopped existing must not keep their leftovers. */
    for (int i = n; i < SPL_MAX_FEET; i++) SP.foot_budget[i] = 0.0f;
}

/* ---------------------------------------------------------------------------
   Rocks (and anything else solid) standing in moving water
   --------------------------------------------------------------------------- */

static void spl_obstacles(float dt) {
    int count = 0;
    int *ents = g3d_scene_impl_get_entities(&count);
    if (!ents || count <= 0) {
        g3d_waterfield_set_obstacles(NULL, 0);
        g3d_water_falls_set_obstacles(NULL, 0);
        return;
    }

    /* Collected while walking the scene and handed to the field in one go, so
       the water actually goes ROUND the rocks instead of through them. The
       splash and the diversion then agree by construction: both come from the
       same list of things standing in the water. */
    static float boxes[SPL_MAX_TRACKED * 4];
    int nboxes = 0;
    /* Y aparte, con ALTURA, lo que estorba a una cascada. No vale la misma
       lista: la de arriba solo recoge lo que esta metido en agua con fondo
       suficiente, y una roca a media pared de una cascada no lo esta nunca --
       el agua le pasa por delante, no por debajo. Ademas una cortina es
       vertical, asi que necesita saber hasta donde llega la piedra, y el mask
       del campo es plano. */
    static float fboxes[SPL_MAX_TRACKED * 6];
    int nfboxes = 0;

    for (int i = 0; i < count; i++) {
        float mn[3], mx[3];
        if (!spl_entity_aabb(ents[i], mn, mx)) continue;

        if (nfboxes < SPL_MAX_TRACKED) {
            const float fcore = 0.8f;
            float fcx = (mn[0] + mx[0]) * 0.5f, fcz = (mn[2] + mx[2]) * 0.5f;
            float frx = (mx[0] - mn[0]) * 0.5f, frz = (mx[2] - mn[2]) * 0.5f;
            if (frx > 0.001f || frz > 0.001f) {
                float *fb = &fboxes[nfboxes * 6];
                fb[0] = fcx - frx * fcore; fb[1] = mn[1]; fb[2] = fcz - frz * fcore;
                fb[3] = fcx + frx * fcore; fb[4] = mx[1]; fb[5] = fcz + frz * fcore;
                nfboxes++;
            }
        }

        float cx = (mn[0] + mx[0]) * 0.5f;
        float cz = (mn[2] + mx[2]) * 0.5f;
        float rx = (mx[0] - mn[0]) * 0.5f;
        float rz = (mx[2] - mn[2]) * 0.5f;
        float radius = (rx > rz ? rx : rz);
        if (radius <= 0.001f) continue;

        float depth = g3d_waterfield_depth_at(cx, cz);
        if (depth <= 0.02f) continue;                 /* not in the water */

        float surface = g3d_waterfield_level_at(cx, cz);
        /* Something fully submerged does not splash: the water closes over it.
           It has to break the surface. */
        if (mx[1] < surface + 0.05f) continue;

        /* It breaks the surface, so it is also in the way. The box is pulled in
           to a CORE rather than used whole: a bounding box always overstates a
           rounded rock, and on something like a tree it would dam the river
           across the full width of the canopy when only the trunk is in it. */
        if (nboxes < SPL_MAX_TRACKED) {
            const float core = 0.7f;
            float *bx = &boxes[nboxes * 4];
            bx[0] = cx - rx * core; bx[1] = cz - rz * core;
            bx[2] = cx + rx * core; bx[3] = cz + rz * core;
            nboxes++;
        }

        float vx = 0.0f, vz = 0.0f;
        g3d_waterfield_flow_at(cx, cz, &vx, &vz);
        float speed = sqrtf(vx*vx + vz*vz);
        if (speed < SP.threshold) continue;           /* still water, no splash */

        if (SP.amount <= 0.0f) continue;      /* still blocks, just does not spray */

        float *budget = spl_budget_for(ents[i]);
        if (!budget) continue;

        /* A wider rock intercepts more water, and faster water throws more of
           it. Both matter, so the rate carries both. */
        float over = speed - SP.threshold;
        float rate = SP.amount * (4.0f + 14.0f * over) * (0.4f + radius * 0.5f);
        if (rate > 60.0f) rate = 60.0f;
        int n = spl_take(budget, rate, dt, 10);
        if (n <= 0) continue;

        /* On the UPSTREAM face, which is the side the water actually piles up
           and breaks on. Spraying from the middle of the rock would look like
           the rock was leaking. */
        float inv = 1.0f / speed;
        float ux = vx * inv, uz = vz * inv;
        float px = cx - ux * radius * 0.9f;
        float pz = cz - uz * radius * 0.9f;
        float py = surface + 0.05f;

        g3d_particles_burst(px, py, pz, n,
                            speed * 0.85f + 0.6f,
                            0.05f + radius * 0.05f,
                            0.35f + over * 0.10f,
                            0.95f, 0.97f, 1.0f);
        SP.emitted++;
    }

    g3d_waterfield_set_obstacles(nboxes ? boxes : NULL, nboxes);
    g3d_water_falls_set_obstacles(nfboxes ? fboxes : NULL, nfboxes);
}

/* --------------------------------------------------------------------------- */

void g3d_water_splash_tick(float dt) {
    if (!g3d_waterfield_active()) return;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;      /* a stall must not dump a cloud at once */

    if (SP.amount > 0.0f) spl_falls(dt);

    /* Turning the droplets off must NOT put the rocks back to being invisible
       to the water: the diversion is simulation, the spray is decoration.
       Every collider in the scene gets its world AABB rebuilt to test it, so
       the scan is throttled rather than run per frame -- at 144 fps that is
       most of the cost for none of the effect. The accumulated dt goes in
       whole, so the droplet rate is unchanged. */
    SP.scan_accum += dt;
    if (SP.scan_accum >= 1.0f / 30.0f) {
        spl_obstacles(SP.scan_accum);
        SP.scan_accum = 0.0f;
    }
}

void g3d_water_splash_set_amount(float amount) {
    SP.amount = amount < 0.0f ? 0.0f : amount;
}

float g3d_water_splash_get_amount(void) { return SP.amount; }

void g3d_water_splash_set_threshold(float speed) {
    SP.threshold = speed < 0.0f ? 0.0f : speed;
}

int g3d_water_splash_debug_count(void) {
    int n = SP.emitted;
    SP.emitted = 0;
    return n;
}

void g3d_water_splash_shutdown(void) {
    SP.ntracked = 0;
    SP.scan_accum = 0.0f;
    SP.emitted = 0;
    memset(SP.foot_budget, 0, sizeof(SP.foot_budget));
}
