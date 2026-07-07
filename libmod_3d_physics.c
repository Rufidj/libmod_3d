/*
 * libmod_3d_physics.c - collision + character controller (see header).
 */
#include "libmod_3d_physics.h"
#include "libmod_3d_scenefile.h"   /* g3d_scene_terrain_height */
#include <math.h>
#include <string.h>

#define MAX_CHARS 32
#define MAX_BOXES 512

typedef struct {
    float px, py, pz;        /* feet position (bottom-centre of the capsule)   */
    float vx, vy, vz;        /* velocity                                       */
    float want_x, want_z;    /* desired horizontal velocity (from input)       */
    float radius, height;
    float step;              /* max ledge height that can be climbed           */
    float slope_cos;         /* cos(max walkable slope); steeper -> slide       */
    int   grounded;
    int   active;
} G3DChar;

typedef struct { float mn[3], mx[3]; int active; } G3DBox;

static G3DChar g_chars[MAX_CHARS];
static G3DBox  g_boxes[MAX_BOXES];
static float   g_gravity = 24.0f;

/* ------------------------------------------------------------------------- */

int g3d_char_create(float x, float y, float z, float radius, float height) {
    for (int i = 0; i < MAX_CHARS; i++) {
        if (g_chars[i].active) continue;
        G3DChar *c = &g_chars[i];
        memset(c, 0, sizeof(*c));
        c->px = x; c->py = y; c->pz = z;
        c->radius = radius > 0.05f ? radius : 0.4f;
        c->height = height > 0.2f ? height : 1.8f;
        c->step = 0.5f;
        c->slope_cos = cosf(48.0f * 3.14159265f / 180.0f);
        c->active = 1;
        return i;
    }
    return -1;
}

void g3d_char_destroy(int id) {
    if (id >= 0 && id < MAX_CHARS) g_chars[id].active = 0;
}
void g3d_char_clear_all(void) {
    for (int i = 0; i < MAX_CHARS; i++) g_chars[i].active = 0;
}

void g3d_char_move(int id, float vx, float vz) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    g_chars[id].want_x = vx; g_chars[id].want_z = vz;
}

void g3d_char_jump(int id, float speed) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (c->grounded) { c->vy = speed; c->grounded = 0; }
}

void g3d_char_set_position(int id, float x, float y, float z) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    c->px = x; c->py = y; c->pz = z; c->vx = c->vy = c->vz = 0.0f;
}

void g3d_char_set_tuning(int id, float step, float slope_deg) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (step >= 0.0f) c->step = step;
    if (slope_deg > 1.0f && slope_deg < 89.0f)
        c->slope_cos = cosf(slope_deg * 3.14159265f / 180.0f);
}

float g3d_char_x(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].px : 0.0f; }
float g3d_char_y(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].py : 0.0f; }
float g3d_char_z(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].pz : 0.0f; }
int   g3d_char_grounded(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].grounded : 0; }

void g3d_physics_set_gravity(float g) { g_gravity = g; }

int g3d_collider_add_box(float minx, float miny, float minz,
                         float maxx, float maxy, float maxz) {
    for (int i = 0; i < MAX_BOXES; i++) {
        if (g_boxes[i].active) continue;
        g_boxes[i].mn[0] = minx < maxx ? minx : maxx;
        g_boxes[i].mn[1] = miny < maxy ? miny : maxy;
        g_boxes[i].mn[2] = minz < maxz ? minz : maxz;
        g_boxes[i].mx[0] = minx > maxx ? minx : maxx;
        g_boxes[i].mx[1] = miny > maxy ? miny : maxy;
        g_boxes[i].mx[2] = minz > maxz ? minz : maxz;
        g_boxes[i].active = 1;
        return i;
    }
    return -1;
}
void g3d_collider_clear(void) {
    for (int i = 0; i < MAX_BOXES; i++) g_boxes[i].active = 0;
}

/* ========================================================================= */
/*  raycast vehicle (arcade: bicycle steering + 4-wheel terrain follow)       */
/* ========================================================================= */

#define MAX_VEHICLES 8

typedef struct {
    float px, py, pz;        /* chassis centre                                 */
    float yaw, pitch, roll;  /* heading + visual tilt (radians)                */
    float speed;             /* signed forward speed                           */
    float vy;                /* vertical velocity (jumps / air)                */
    float steer;             /* current steer angle (radians, smoothed)        */
    int   airborne;
    /* geometry + tuning */
    float wheelbase, track, ride;
    float engine, top_speed, max_steer, brake_force, drag;
    int   active;
} G3DVehicle;

static G3DVehicle g_veh[MAX_VEHICLES];

int g3d_vehicle_create(float x, float y, float z, float heading) {
    for (int i = 0; i < MAX_VEHICLES; i++) {
        if (g_veh[i].active) continue;
        G3DVehicle *v = &g_veh[i];
        memset(v, 0, sizeof(*v));
        v->px = x; v->py = y; v->pz = z; v->yaw = heading;
        v->wheelbase = 4.0f; v->track = 2.4f; v->ride = 1.0f;
        v->engine = 26.0f; v->top_speed = 55.0f;
        v->max_steer = 32.0f * 3.14159265f / 180.0f;
        v->brake_force = 40.0f; v->drag = 0.8f;
        v->active = 1;
        return i;
    }
    return -1;
}
void g3d_vehicle_destroy(int id) { if (id>=0 && id<MAX_VEHICLES) g_veh[id].active = 0; }

void g3d_vehicle_set_geometry(int id, float wheelbase, float track, float ride) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (wheelbase > 0.1f) v->wheelbase = wheelbase;
    if (track > 0.1f)     v->track = track;
    if (ride >= 0.0f)     v->ride = ride;
}
void g3d_vehicle_set_tuning(int id, float engine, float top_speed,
                            float max_steer_deg, float brake_force, float drag) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (engine > 0.0f)      v->engine = engine;
    if (top_speed > 0.0f)   v->top_speed = top_speed;
    if (max_steer_deg > 0.0f) v->max_steer = max_steer_deg * 3.14159265f / 180.0f;
    if (brake_force > 0.0f) v->brake_force = brake_force;
    if (drag > 0.0f)        v->drag = drag;
}

float g3d_vehicle_x(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].px:0.0f; }
float g3d_vehicle_y(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].py:0.0f; }
float g3d_vehicle_z(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].pz:0.0f; }
float g3d_vehicle_yaw(int id)   { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].yaw:0.0f; }
float g3d_vehicle_pitch(int id) { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].pitch:0.0f; }
float g3d_vehicle_roll(int id)  { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].roll:0.0f; }
float g3d_vehicle_speed(int id) { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].speed:0.0f; }

void g3d_vehicle_update(int id, float dt, float throttle, float steer_in, float brake) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (dt <= 0.0f) return; if (dt > 0.1f) dt = 0.1f;
    if (throttle < -1.0f) throttle = -1.0f; if (throttle > 1.0f) throttle = 1.0f;
    if (steer_in < -1.0f) steer_in = -1.0f; if (steer_in > 1.0f) steer_in = 1.0f;
    if (brake < 0.0f) brake = 0.0f; if (brake > 1.0f) brake = 1.0f;

    /* steering: ease toward target, less authority at high speed */
    float sp_frac = fabsf(v->speed) / (v->top_speed + 1e-3f);
    float target = steer_in * v->max_steer * (1.0f - 0.4f * sp_frac);
    float k = 8.0f * dt; if (k > 1.0f) k = 1.0f;
    v->steer += (target - v->steer) * k;

    /* engine + brake + drag (only drive when on the ground) */
    if (!v->airborne) v->speed += throttle * v->engine * dt;
    if (brake > 0.0f) {
        float b = brake * v->brake_force * dt;
        if (v->speed > 0.0f) v->speed = v->speed > b ? v->speed - b : 0.0f;
        else                 v->speed = v->speed < -b ? v->speed + b : 0.0f;
    }
    v->speed -= v->speed * v->drag * dt;                 /* aero + rolling drag */
    if (throttle == 0.0f && fabsf(v->speed) < 0.15f) v->speed = 0.0f;
    if (v->speed >  v->top_speed)        v->speed =  v->top_speed;
    if (v->speed < -v->top_speed * 0.4f) v->speed = -v->top_speed * 0.4f;

    /* heading via bicycle model (only steers while rolling on the ground) */
    if (!v->airborne && fabsf(v->speed) > 0.2f)
        v->yaw += (v->speed / v->wheelbase) * tanf(v->steer) * dt;

    float fwx = sinf(v->yaw), fwz = cosf(v->yaw);
    float rgx = cosf(v->yaw), rgz = -sinf(v->yaw);       /* body right (matches demo axes) */

    /* horizontal move along heading (full grip; drift is a later refinement) */
    v->px += fwx * v->speed * dt;
    v->pz += fwz * v->speed * dt;

    /* sample the ground under the four wheels */
    float hb = v->wheelbase * 0.5f, tr = v->track * 0.5f;
    float wx, wz;
    wx = v->px + fwx*hb - rgx*tr; wz = v->pz + fwz*hb - rgz*tr; float gFL = g3d_scene_terrain_height(wx, wz);
    wx = v->px + fwx*hb + rgx*tr; wz = v->pz + fwz*hb + rgz*tr; float gFR = g3d_scene_terrain_height(wx, wz);
    wx = v->px - fwx*hb - rgx*tr; wz = v->pz - fwz*hb - rgz*tr; float gRL = g3d_scene_terrain_height(wx, wz);
    wx = v->px - fwx*hb + rgx*tr; wz = v->pz - fwz*hb + rgz*tr; float gRR = g3d_scene_terrain_height(wx, wz);
    float front = (gFL + gFR) * 0.5f, rear = (gRL + gRR) * 0.5f;
    float left  = (gFL + gRL) * 0.5f, rightg = (gFR + gRR) * 0.5f;
    float avg   = (gFL + gFR + gRL + gRR) * 0.25f;
    float desired = avg + v->ride;

    /* vertical: glue to the ground while driving (no per-bump jitter); only go
       airborne off a real ledge or a strong ramp at speed. */
    if (!v->airborne) {
        float climb = (desired - v->py) / dt;            /* how fast the ground rises under us */
        if (desired < v->py - 0.6f) {                    /* drove off a ledge -> fall */
            v->airborne = 1; v->vy = 0.0f;
        } else if (climb > 8.0f && v->speed > 8.0f) {    /* real ramp at speed -> launch */
            v->airborne = 1; v->vy = climb * 0.5f;
        } else {
            v->py = desired; v->vy = 0.0f;               /* glued to the ground */
        }
    }
    if (v->airborne) {                                   /* ballistic until we land */
        v->vy -= g_gravity * dt;
        float ny = v->py + v->vy * dt;
        if (v->vy <= 0.0f && ny <= desired) { v->py = desired; v->vy = 0.0f; v->airborne = 0; }
        else v->py = ny;
    }

    /* visual tilt from the wheel heights (only meaningful on the ground) */
    float tp = atanf((rear - front) / v->wheelbase);
    float tr2 = atanf((rightg - left) / v->track);
    if (v->airborne) { tp = v->pitch; tr2 = v->roll; }   /* hold last tilt in the air */
    float ks = 8.0f * dt; if (ks > 1.0f) ks = 1.0f;
    v->pitch += (tp  - v->pitch) * ks;                   /* smooth so bumps don't jitter */
    v->roll  += (tr2 - v->roll)  * ks;
}

/* ------------------------------------------------------------------------- */

/* Push a vertical cylinder (centre cx,cz, radius r, y-span [y0,y1]) out of a
   box in the XZ plane. Returns 1 if it moved the cylinder. */
static int resolve_xz(const G3DBox *b, float *cx, float *cz, float r,
                      float y0, float y1) {
    if (y1 <= b->mn[1] || y0 >= b->mx[1]) return 0;          /* no vertical overlap */
    /* closest point on the box rectangle to the circle centre */
    float qx = *cx < b->mn[0] ? b->mn[0] : (*cx > b->mx[0] ? b->mx[0] : *cx);
    float qz = *cz < b->mn[2] ? b->mn[2] : (*cz > b->mx[2] ? b->mx[2] : *cz);
    float dx = *cx - qx, dz = *cz - qz;
    float d2 = dx*dx + dz*dz;
    if (d2 >= r*r) return 0;                                  /* no penetration */
    if (d2 > 1e-8f) {                                         /* outside: push along normal */
        float d = sqrtf(d2);
        *cx += dx / d * (r - d);
        *cz += dz / d * (r - d);
    } else {                                                  /* centre inside: push out shortest axis */
        float pxp = b->mx[0] - *cx, pxn = *cx - b->mn[0];
        float pzp = b->mx[2] - *cz, pzn = *cz - b->mn[2];
        float m = pxp; int axis = 0; float sign = 1.0f;
        if (pxn < m) { m = pxn; axis = 0; sign = -1.0f; }
        if (pzp < m) { m = pzp; axis = 2; sign = 1.0f; }
        if (pzn < m) { m = pzn; axis = 2; sign = -1.0f; }
        if (axis == 0) *cx += sign * (m + r); else *cz += sign * (m + r);
    }
    return 1;
}

/* Ground height under a cylinder footprint: terrain + tops of boxes we can stand
   on (top no higher than feet+step). */
static float ground_under(float cx, float cz, float r, float feet, float step) {
    float g = g3d_scene_terrain_height(cx, cz);
    for (int i = 0; i < MAX_BOXES; i++) {
        const G3DBox *b = &g_boxes[i];
        if (!b->active) continue;
        if (b->mx[1] > feet + step + 0.05f) continue;        /* too tall to step onto */
        /* footprint overlaps box in XZ (expanded by radius)? */
        float qx = cx < b->mn[0] ? b->mn[0] : (cx > b->mx[0] ? b->mx[0] : cx);
        float qz = cz < b->mn[2] ? b->mn[2] : (cz > b->mx[2] ? b->mx[2] : cz);
        float dx = cx - qx, dz = cz - qz;
        if (dx*dx + dz*dz > r*r) continue;
        if (b->mx[1] > g) g = b->mx[1];
    }
    return g;
}

void g3d_char_update(int id, float dt) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;

    /* terrain normal at the current spot -> is this slope too steep to stand on? */
    float e = c->radius > 0.3f ? c->radius : 0.3f;
    float hL = g3d_scene_terrain_height(c->px - e, c->pz);
    float hR = g3d_scene_terrain_height(c->px + e, c->pz);
    float hD = g3d_scene_terrain_height(c->px, c->pz - e);
    float hU = g3d_scene_terrain_height(c->px, c->pz + e);
    float nx = hL - hR, nz = hD - hU, ny = 2.0f * e;
    float nlen = sqrtf(nx*nx + ny*ny + nz*nz); if (nlen < 1e-5f) nlen = 1.0f;
    float slope_y = ny / nlen;                                /* 1 = flat, small = steep */
    int steep = slope_y < c->slope_cos;

    /* horizontal control: full control on walkable ground, momentum + light
       air control otherwise, downhill slide on steep ground. */
    if (c->grounded && !steep) {
        c->vx = c->want_x; c->vz = c->want_z;
    } else {
        float k = 2.5f * dt; if (k > 1.0f) k = 1.0f;          /* light air control */
        c->vx += (c->want_x - c->vx) * k;
        c->vz += (c->want_z - c->vz) * k;
        if (c->grounded && steep) {                           /* slide downhill */
            float dgx = (hR - hL), dgz = (hU - hD);           /* downhill direction (XZ) */
            float dl = sqrtf(dgx*dgx + dgz*dgz);
            if (dl > 1e-5f) { c->vx += dgx/dl * g_gravity * dt; c->vz += dgz/dl * g_gravity * dt; }
        }
    }

    /* gravity */
    c->vy -= g_gravity * dt;

    /* --- horizontal integrate + collide-and-slide against boxes --- */
    float nxp = c->px + c->vx * dt;
    float nzp = c->pz + c->vz * dt;
    float y0 = c->py + c->step;                               /* ignore steppable low boxes here */
    float y1 = c->py + c->height;
    for (int i = 0; i < MAX_BOXES; i++) {
        if (!g_boxes[i].active) continue;
        if (resolve_xz(&g_boxes[i], &nxp, &nzp, c->radius, y0, y1)) {
            /* kill velocity into the wall so we slide along it, not through it */
            c->vx = (nxp - c->px) / dt;
            c->vz = (nzp - c->pz) / dt;
        }
    }
    c->px = nxp; c->pz = nzp;

    /* --- vertical integrate + ground/ceiling --- */
    float ground = ground_under(c->px, c->pz, c->radius, c->py, c->step);
    float ny_new = c->py + c->vy * dt;

    if (c->vy <= 0.0f && ny_new <= ground) {                  /* landing / standing */
        c->py = ground;
        c->vy = 0.0f;
        c->grounded = !steep;                                 /* steep: keep sliding */
    } else {
        /* ceiling: head hits the underside of a box */
        float head0 = c->py + c->height, head1 = ny_new + c->height;
        for (int i = 0; i < MAX_BOXES; i++) {
            const G3DBox *b = &g_boxes[i];
            if (!b->active || c->vy <= 0.0f) continue;
            if (b->mn[1] < head1 && b->mn[1] >= head0) {
                float qx = c->px < b->mn[0] ? b->mn[0] : (c->px > b->mx[0] ? b->mx[0] : c->px);
                float qz = c->pz < b->mn[2] ? b->mn[2] : (c->pz > b->mx[2] ? b->mx[2] : c->pz);
                float dx = c->px - qx, dz = c->pz - qz;
                if (dx*dx + dz*dz <= c->radius*c->radius) {
                    ny_new = b->mn[1] - c->height; c->vy = 0.0f;
                }
            }
        }
        c->py = ny_new;
        c->grounded = 0;
    }
}
