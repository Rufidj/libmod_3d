/*
 * libmod_3d_road.c - Carreteras: linea central a partir de nodos y consultas. Ver el .h.
 *
 * El algoritmo (curvas Hermite con tangentes Catmull-Rom, seguimiento del terreno con
 * suavizado y pendiente maxima) es el mismo que usaba el modo Carreras del editor, sin
 * muros, saltos, rampas ni nada de reglas de juego.
 */
#include "libmod_3d_road.h"
#include "libmod_3d_scenefile.h"   /* g3d_scene_terrain_height_raw */
#include "libmod_3d_mesh.h"
#include "libmod_3d_material.h"
#include "libmod_3d_entity.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define G3D_ROAD_SURF 8     /* superficies distinguidas al dibujar (la etiqueta se recorta a 0..7) */
#define PEND_MAX   0.40f   /* pendiente maxima (subida o bajada) de la carretera que sigue al terreno */
#define PASO       2.0f    /* separacion aproximada entre muestras, en metros */

typedef struct { float x, z, y, w, bank; int follow; float dy; int sup; float tx, tz; } Node;   /* tx,tz: tangente propia (0,0 = automatica) */
typedef struct {
    float c[3];       /* centro */
    float t[3];       /* tangente (unitaria) */
    float l[2];       /* lateral izquierdo en XZ (unitario) */
    float w, dist, bank;
    int   sup;
} Sample;
typedef struct {
    int active, closed, built;
    Node n[G3D_ROAD_MAX_NODES]; int nn;
    Sample *s; int ns;
    float length;
    float bb[4];                       /* caja (minx, maxx, minz, maxz) para descartar rapido en g3d_road_ground */
    int   ground;                      /* 1 = la carretera cuenta como suelo (por defecto NO: es solo informacion) */
    int   ent[G3D_ROAD_SURF];          /* entidad dibujada de cada superficie (-1 = ninguna) */
    int   mat[G3D_ROAD_SURF];
} Road;

static Road g_road[G3D_ROAD_MAX];

static int ok(int r) { return r >= 0 && r < G3D_ROAD_MAX && g_road[r].active; }
static int built(int r) { return ok(r) && g_road[r].built && g_road[r].ns >= 2; }

int g3d_road_create(int closed) {
    for (int i = 0; i < G3D_ROAD_MAX; i++) {
        if (g_road[i].active) continue;
        free(g_road[i].s);
        memset(&g_road[i], 0, sizeof(Road));
        g_road[i].active = 1; g_road[i].closed = closed ? 1 : 0;
        for (int k = 0; k < G3D_ROAD_SURF; k++) { g_road[i].ent[k] = -1; g_road[i].mat[k] = -1; }
        return i;
    }
    return -1;
}
void g3d_road_destroy(int r) { if (!ok(r)) return; g3d_road_hide(r); free(g_road[r].s); memset(&g_road[r], 0, sizeof(Road)); }
void g3d_road_clear(void)    { for (int i = 0; i < G3D_ROAD_MAX; i++) g3d_road_destroy(i); }

int g3d_road_add_node(int r, float x, float z, float y, float width, float bank_deg,
                      int follow, float dy, int surface) {
    if (!ok(r) || g_road[r].nn >= G3D_ROAD_MAX_NODES) return -1;
    Node *n = &g_road[r].n[g_road[r].nn];
    n->x = x; n->z = z; n->y = y; n->w = width; n->bank = bank_deg;
    n->follow = follow ? 1 : 0; n->dy = dy; n->sup = surface; n->tx = n->tz = 0.0f;
    g_road[r].built = 0;
    return g_road[r].nn++;
}

/* tangente automatica (Catmull-Rom) de un nodo segun sus vecinos */
static void tangente(const Road *R, int i, float *tx, float *tz) {
    if (R->n[i].tx != 0.0f || R->n[i].tz != 0.0f) { *tx = R->n[i].tx; *tz = R->n[i].tz; return; }
    int n = R->nn, ip = i - 1, in = i + 1;
    if (R->closed) { ip = (i - 1 + n) % n; in = (i + 1) % n; }
    int hp = ip >= 0, hn = in < n;
    const Node *p = &R->n[hp ? ip : i], *q = &R->n[hn ? in : i];
    float k = (hp && hn) ? 0.5f : 1.0f;
    *tx = (q->x - p->x) * k; *tz = (q->z - p->z) * k;
}

typedef struct { float x, y, z, b, w, dy; int sg, sup; } Raw;

static float maxf(float a, float b) { return a > b ? a : b; }

void g3d_road_set_tangent(int r, int node, float tx, float tz) {
    if (!ok(r) || node < 0 || node >= g_road[r].nn) return;
    g_road[r].n[node].tx = tx; g_road[r].n[node].tz = tz; g_road[r].built = 0;
}

int g3d_road_build(int r) {
    if (!ok(r)) return 0;
    Road *R = &g_road[r];
    g3d_road_hide(r);
    free(R->s); R->s = NULL; R->ns = 0; R->built = 0; R->length = 0;
    int n = R->nn, closed = R->closed && n >= 3;
    int ntr = closed ? n : (n >= 2 ? n - 1 : 0);
    if (ntr < 1) return 0;

    /* --- puntos de la curva --- */
    int cap = 64, N = 0;
    Raw *raw = (Raw *)malloc(sizeof(Raw) * cap);
    if (!raw) return 0;
    for (int k = 0; k < ntr; k++) {
        const Node *A = &R->n[k], *B = &R->n[(k + 1) % n];
        float tax, taz, tbx, tbz; tangente(R, k, &tax, &taz); tangente(R, (k + 1) % n, &tbx, &tbz);
        float chord = hypotf(B->x - A->x, B->z - A->z);
        int ns = (int)ceilf(chord * 1.2f / PASO); if (ns < 2) ns = 2;
        int last = (!closed && k == ntr - 1) ? ns : ns - 1;      /* una carretera abierta acaba en su ultimo nodo */
        for (int q = 0; q <= last; q++) {
            if (N >= cap) { cap *= 2; Raw *nr = (Raw *)realloc(raw, sizeof(Raw) * cap); if (!nr) { free(raw); return 0; } raw = nr; }
            float t = (float)q / ns, u = t * t * (3 - 2 * t);
            float h00 = 2*t*t*t - 3*t*t + 1, h10 = t*t*t - 2*t*t + t, h01 = -2*t*t*t + 3*t*t, h11 = t*t*t - t*t;
            Raw *o = &raw[N++];
            o->x = h00 * A->x + h10 * tax + h01 * B->x + h11 * tbx;
            o->z = h00 * A->z + h10 * taz + h01 * B->z + h11 * tbz;
            o->y = A->y + (B->y - A->y) * u;
            o->sg = (A->follow && B->follow); o->dy = A->dy + (B->dy - A->dy) * u;
            o->b = A->bank + (B->bank - A->bank) * u;
            o->w = maxf(4.0f, A->w + (B->w - A->w) * u);
            o->sup = A->sup;
        }
    }
    if (N < 2) { free(raw); return 0; }

    /* --- la carretera pegada al terreno --- */
    int hay_sg = 0; for (int i = 0; i < N; i++) if (raw[i].sg) { hay_sg = 1; break; }
    if (hay_sg) {
        float *hm = (float *)calloc((size_t)N, sizeof(float));
        if (!hm) { free(raw); return 0; }
        for (int i = 0; i < N; i++) {                      /* el relieve de TODA la huella: que no asome por el asfalto */
            if (!raw[i].sg) continue;
            int ip = closed ? (i - 1 + N) % N : (i > 0 ? i - 1 : 0), in = closed ? (i + 1) % N : (i < N - 1 ? i + 1 : N - 1);
            float tx = raw[in].x - raw[ip].x, tz = raw[in].z - raw[ip].z, l = hypotf(tx, tz); if (l < 1e-4f) l = 1;
            float lx = tz / l, lz = -tx / l, fx = tx / l, fz = tz / l, w = raw[i].w * 0.5f, mx = -1e30f;
            for (int la = -2; la <= 2; la++) for (int lo = -1; lo <= 1; lo++)
                mx = maxf(mx, g3d_scene_terrain_height_raw(raw[i].x + lx * w * 0.5f * la + fx * 1.25f * lo,
                                                        raw[i].z + lz * w * 0.5f * la + fz * 1.25f * lo));
            hm[i] = mx;
        }
        for (int i = 0; i < N; i++) {                      /* suavizado a lo largo */
            if (!raw[i].sg) continue;
            float acc = 0; int cnt = 0;
            for (int q = -3; q <= 3; q++) {
                int j = closed ? ((i + q) % N + N) % N : i + q;
                if (j < 0 || j >= N || !raw[j].sg) continue;
                acc += hm[j]; cnt++;
            }
            raw[i].y = maxf(cnt ? acc / cnt : hm[i], hm[i]) + raw[i].dy;
        }
        free(hm);
        /* pendiente maxima: solo se SUBE lo bajo, asi nunca queda enterrada */
        for (int it = 0; it < 3; it++) {
            for (int q = 0; q < (closed ? 2 * N : N - 1); q++) {
                int i = q % N, j = (i + 1) % N; if (!closed && j == 0) break;
                if (raw[i].sg && raw[j].sg)
                    raw[j].y = maxf(raw[j].y, raw[i].y - PEND_MAX * maxf(0.5f, hypotf(raw[j].x - raw[i].x, raw[j].z - raw[i].z)));
            }
            for (int q = 0; q < (closed ? 2 * N : N - 1); q++) {
                int i = ((N - 1 - q) % N + N) % N, j = (i - 1 + N) % N; if (!closed && i == 0) break;
                if (raw[i].sg && raw[j].sg)
                    raw[j].y = maxf(raw[j].y, raw[i].y - PEND_MAX * maxf(0.5f, hypotf(raw[j].x - raw[i].x, raw[j].z - raw[i].z)));
            }
        }
    }

    /* --- muestras --- */
    Sample *S = (Sample *)malloc(sizeof(Sample) * (size_t)N);
    if (!S) { free(raw); return 0; }
    for (int i = 0; i < N; i++) {
        int ip = closed ? (i - 1 + N) % N : (i > 0 ? i - 1 : 0), in = closed ? (i + 1) % N : (i < N - 1 ? i + 1 : N - 1);
        float tx = raw[in].x - raw[ip].x, ty = raw[in].y - raw[ip].y, tz = raw[in].z - raw[ip].z;
        float l = sqrtf(tx*tx + ty*ty + tz*tz); if (l < 1e-6f) { tx = 0; ty = 0; tz = 1; l = 1; }
        tx /= l; ty /= l; tz /= l;
        float lh = hypotf(tz, tx); if (lh < 1e-6f) lh = 1;
        float b = raw[i].b * 3.14159265f / 180.0f, w = raw[i].w * 0.5f;
        Sample *s = &S[i];
        s->c[0] = raw[i].x; s->c[1] = raw[i].y + fabsf(sinf(b)) * w; s->c[2] = raw[i].z;   /* el borde bajo no se hunde */
        s->t[0] = tx; s->t[1] = ty; s->t[2] = tz;
        s->l[0] = tz / lh; s->l[1] = -tx / lh;             /* +X con rumbo +Z = izquierda del que avanza */
        s->w = raw[i].w; s->bank = raw[i].b; s->sup = raw[i].sup;
        s->dist = i == 0 ? 0.0f : S[i-1].dist + sqrtf((s->c[0]-S[i-1].c[0])*(s->c[0]-S[i-1].c[0]) +
                                                      (s->c[1]-S[i-1].c[1])*(s->c[1]-S[i-1].c[1]) +
                                                      (s->c[2]-S[i-1].c[2])*(s->c[2]-S[i-1].c[2]));
    }
    float len = S[N-1].dist;
    if (closed) {
        float dx = S[0].c[0]-S[N-1].c[0], dy = S[0].c[1]-S[N-1].c[1], dz = S[0].c[2]-S[N-1].c[2];
        len += sqrtf(dx*dx + dy*dy + dz*dz);
    }
    free(raw);
    R->s = S; R->ns = N; R->length = len; R->built = 1; R->closed = closed;
    float mw = 0; R->bb[0] = R->bb[2] = 1e30f; R->bb[1] = R->bb[3] = -1e30f;
    for (int i = 0; i < N; i++) {
        if (S[i].w > mw) mw = S[i].w;
        if (S[i].c[0] < R->bb[0]) R->bb[0] = S[i].c[0];
        if (S[i].c[0] > R->bb[1]) R->bb[1] = S[i].c[0];
        if (S[i].c[2] < R->bb[2]) R->bb[2] = S[i].c[2];
        if (S[i].c[2] > R->bb[3]) R->bb[3] = S[i].c[2];
    }
    R->bb[0] -= mw; R->bb[1] += mw; R->bb[2] -= mw; R->bb[3] += mw;
    return N;
}

int   g3d_road_count(void)       { int c = 0; for (int i = 0; i < G3D_ROAD_MAX; i++) if (built(i)) c++; return c; }
int   g3d_road_nodes(int r)      { return ok(r) ? g_road[r].nn : 0; }
int   g3d_road_closed(int r)     { return ok(r) ? g_road[r].closed : 0; }
float g3d_road_length(int r)     { return built(r) ? g_road[r].length : 0.0f; }

/* Muestra interpolada entre la muestra i y la siguiente, con fraccion f (0..1). */
typedef struct { float c[3], t[3], l[2], w, bank, dist; int sup; } Pt;
static void interp(const Road *R, int i, float f, Pt *o) {
    const Sample *a = &R->s[i];
    const Sample *b = (i + 1 < R->ns) ? &R->s[i + 1] : (R->closed ? &R->s[0] : a);
    float db = (i + 1 < R->ns) ? b->dist : R->length;
    for (int k = 0; k < 3; k++) { o->c[k] = a->c[k] + (b->c[k] - a->c[k]) * f; o->t[k] = a->t[k] + (b->t[k] - a->t[k]) * f; }
    for (int k = 0; k < 2; k++) o->l[k] = a->l[k] + (b->l[k] - a->l[k]) * f;
    o->w = a->w + (b->w - a->w) * f; o->bank = a->bank + (b->bank - a->bank) * f; o->sup = a->sup;
    o->dist = a->dist + (db - a->dist) * f;
}

/* Muestra a la distancia d. Devuelve 0 si no hay carretera. */
static int along(int r, float d, Pt *o) {
    if (!built(r)) return 0;
    const Road *R = &g_road[r];
    float L = R->length;
    if (R->closed) { d = fmodf(d, L); if (d < 0) d += L; }
    else           { if (d < 0) d = 0; if (d > R->s[R->ns - 1].dist) d = R->s[R->ns - 1].dist; }
    int lo = 0, hi = R->ns - 1;                            /* ultima muestra con dist <= d */
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (R->s[mid].dist <= d) lo = mid; else hi = mid - 1; }
    float db = (lo + 1 < R->ns) ? R->s[lo + 1].dist : L;
    float seg = db - R->s[lo].dist, f = seg > 1e-6f ? (d - R->s[lo].dist) / seg : 0.0f;
    if (f < 0) { f = 0; }
    if (f > 1) { f = 1; }
    interp(R, lo, f, o);
    return 1;
}

float g3d_road_x(int r, float d)     { Pt p; return along(r, d, &p) ? p.c[0] : 0.0f; }
float g3d_road_y(int r, float d)     { Pt p; return along(r, d, &p) ? p.c[1] : 0.0f; }
float g3d_road_z(int r, float d)     { Pt p; return along(r, d, &p) ? p.c[2] : 0.0f; }
float g3d_road_width(int r, float d) { Pt p; return along(r, d, &p) ? p.w : 0.0f; }
int   g3d_road_surface(int r, float d) { Pt p; return along(r, d, &p) ? p.sup : 0; }
float g3d_road_yaw(int r, float d)   { Pt p; return along(r, d, &p) ? atan2f(p.t[0], p.t[2]) : 0.0f; }

/* ---- el punto de la carretera mas cercano ---- */
/* Segmento (muestra i -> i+1) mas cercano a (x, z) de entre todas las carreteras. Con use_bb se saltan las
   carreteras cuya caja no contiene el punto (para las consultas de suelo, que son muchas por frame). */
static int project(float x, float z, int use_bb, int *br, int *bi, float *bt) {
    float best = 1e30f; int found = 0;
    for (int r = 0; r < G3D_ROAD_MAX; r++) {
        if (!built(r)) continue;
        const Road *R = &g_road[r];
        if (use_bb && (!R->ground || x < R->bb[0] || x > R->bb[1] || z < R->bb[2] || z > R->bb[3])) continue;
        int nseg = R->closed ? R->ns : R->ns - 1;
        for (int i = 0; i < nseg; i++) {
            const Sample *a = &R->s[i], *b = &R->s[(i + 1) % R->ns];
            float ex = b->c[0] - a->c[0], ez = b->c[2] - a->c[2], l2 = ex*ex + ez*ez;
            float t = l2 > 1e-9f ? ((x - a->c[0]) * ex + (z - a->c[2]) * ez) / l2 : 0.0f;
            if (t < 0) { t = 0; }
            if (t > 1) { t = 1; }
            float px = a->c[0] + ex * t - x, pz = a->c[2] + ez * t - z, d2 = px*px + pz*pz;
            if (d2 < best) { best = d2; *br = r; *bi = i; *bt = t; found = 1; }
        }
    }
    return found;
}

static struct { int road; float dist, off, y, w, yaw; int sup, on; } g_near = { -1, 0, 0, 0, 0, 0, 0, 0 };

int g3d_road_nearest(float x, float z) {
    int br, bi; float bt;
    if (!project(x, z, 0, &br, &bi, &bt)) { g_near.road = -1; return -1; }
    Pt p; interp(&g_road[br], bi, bt, &p);
    g_near.road = br; g_near.dist = p.dist;
    g_near.off = (x - p.c[0]) * p.l[0] + (z - p.c[2]) * p.l[1];
    g_near.y = p.c[1]; g_near.w = p.w; g_near.sup = p.sup;
    g_near.yaw = atan2f(p.t[0], p.t[2]);
    g_near.on = fabsf(g_near.off) <= p.w * 0.5f;
    return br;
}
int g3d_road_ground(float x, float z, float *h) {
    int br, bi; float bt;
    if (!project(x, z, 1, &br, &bi, &bt)) return 0;
    Pt p; interp(&g_road[br], bi, bt, &p);
    float off = (x - p.c[0]) * p.l[0] + (z - p.c[2]) * p.l[1];
    if (fabsf(off) > p.w * 0.5f) return 0;
    *h = p.c[1] - tanf(p.bank * 3.14159265f / 180.0f) * off;   /* con peralte la superficie esta inclinada */
    return 1;
}
float g3d_road_near_dist(void)   { return g_near.road >= 0 ? g_near.dist : 0.0f; }
float g3d_road_near_offset(void) { return g_near.road >= 0 ? g_near.off : 0.0f; }
float g3d_road_near_y(void)      { return g_near.road >= 0 ? g_near.y : 0.0f; }
float g3d_road_near_width(void)  { return g_near.road >= 0 ? g_near.w : 0.0f; }
int   g3d_road_near_surface(void){ return g_near.road >= 0 ? g_near.sup : 0; }
int   g3d_road_near_on(void)     { return g_near.road >= 0 ? g_near.on : 0; }
float g3d_road_near_yaw(void)    { return g_near.road >= 0 ? g_near.yaw : 0.0f; }

/* ---- dibujo ---- */
static float g_col[G3D_ROAD_SURF][3] = {
    {0.17f, 0.17f, 0.19f},   /* 0 asfalto */
    {0.45f, 0.33f, 0.20f},   /* 1 tierra  */
    {0.72f, 0.86f, 0.95f},   /* 2 hielo   */
    {0.28f, 0.20f, 0.12f},   /* 3 barro   */
    {0.50f, 0.50f, 0.50f}, {0.50f, 0.50f, 0.50f}, {0.50f, 0.50f, 0.50f}, {0.50f, 0.50f, 0.50f} };

void g3d_road_set_color(int s, float r, float g, float b) {
    if (s < 0 || s >= G3D_ROAD_SURF) return;
    g_col[s][0] = r; g_col[s][1] = g; g_col[s][2] = b;
    for (int i = 0; i < G3D_ROAD_MAX; i++)
        if (ok(i) && g_road[i].mat[s] >= 0) g3d_material_impl_set_color(g_road[i].mat[s], r, g, b, 1.0f);
}
int g3d_road_entity(int r, int surface) {
    if (!ok(r) || surface < 0 || surface >= G3D_ROAD_SURF) return -1;
    return g_road[r].ent[surface];
}
void g3d_road_set_ground(int r, int on) { if (ok(r)) g_road[r].ground = on ? 1 : 0; }

void g3d_road_hide(int r) {
    if (!ok(r)) return;
    for (int k = 0; k < G3D_ROAD_SURF; k++) {
        if (g_road[r].ent[k] >= 0) g3d_entity_impl_destroy(g_road[r].ent[k]);
        g_road[r].ent[k] = -1; g_road[r].mat[k] = -1;
    }
}

/* Los dos bordes de una muestra y la normal de la superficie (inclinada por el peralte). */
static void bordes(const Sample *s, float L[3], float Rr[3], float N[3]) {
    float hw = s->w * 0.5f, b = s->bank * 3.14159265f / 180.0f, cb = cosf(b), sb = sinf(b);
    L[0]  = s->c[0] + s->l[0] * cb * hw; L[1]  = s->c[1] - sb * hw; L[2]  = s->c[2] + s->l[1] * cb * hw;
    Rr[0] = s->c[0] - s->l[0] * cb * hw; Rr[1] = s->c[1] + sb * hw; Rr[2] = s->c[2] - s->l[1] * cb * hw;
    float tb = tanf(b); N[0] = tb * s->l[0]; N[1] = 1.0f; N[2] = tb * s->l[1];
    float l = sqrtf(N[0]*N[0] + 1.0f + N[2]*N[2]); N[0] /= l; N[1] /= l; N[2] /= l;
}

int g3d_road_show(int r, int scene) {
    if (!built(r)) return 0;
    g3d_road_hide(r);
    Road *R = &g_road[r];
    int nseg = R->closed ? R->ns : R->ns - 1, made = 0;
    for (int su = 0; su < G3D_ROAD_SURF; su++) {
        int cnt = 0;
        for (int i = 0; i < nseg; i++) { int q = R->s[i].sup; if (q < 0) q = 0; if (q >= G3D_ROAD_SURF) q = G3D_ROAD_SURF - 1; if (q == su) cnt++; }
        if (!cnt) continue;
        G3DVertex *V = (G3DVertex *)malloc(sizeof(G3DVertex) * (size_t)cnt * 4);
        uint32_t *I = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)cnt * 6);
        if (!V || !I) { free(V); free(I); continue; }
        int vi = 0, ii = 0;
        for (int i = 0; i < nseg; i++) {
            int q = R->s[i].sup; if (q < 0) q = 0; if (q >= G3D_ROAD_SURF) q = G3D_ROAD_SURF - 1;
            if (q != su) continue;
            const Sample *a = &R->s[i], *b = &R->s[(i + 1) % R->ns];
            float La[3], Ra[3], Na[3], Lb[3], Rb[3], Nb[3];
            bordes(a, La, Ra, Na); bordes(b, Lb, Rb, Nb);
            float va = a->dist / 8.0f, vb = (i + 1 < R->ns ? b->dist : R->length) / 8.0f;   /* la textura se repite cada 8 m */
            float *P[4] = { Ra, La, Rb, Lb }, *Nn[4] = { Na, Na, Nb, Nb };
            float u[4] = { 0, 1, 0, 1 }, v[4] = { va, va, vb, vb };
            for (int k = 0; k < 4; k++) {
                memcpy(V[vi + k].position, P[k], sizeof(float) * 3);
                memcpy(V[vi + k].normal, Nn[k], sizeof(float) * 3);
                V[vi + k].texcoord[0] = u[k]; V[vi + k].texcoord[1] = v[k];
            }
            /* 0=R_i 1=L_i 2=R_j 3=L_j ; antihorario visto desde arriba */
            I[ii++] = vi + 0; I[ii++] = vi + 2; I[ii++] = vi + 1;
            I[ii++] = vi + 1; I[ii++] = vi + 2; I[ii++] = vi + 3;
            vi += 4;
        }
        G3DMesh *mesh = g3d_mesh_create("Road", V, (uint32_t)vi, I, (uint32_t)ii);
        free(V); free(I);
        if (!mesh) continue;
        g3d_mesh_upload_gpu(mesh);
        int mat = g3d_material_impl_create();
        if (mat >= 0) {
            g3d_material_impl_set_color(mat, g_col[su][0], g_col[su][1], g_col[su][2], 1.0f);
            g3d_material_impl_set_roughness(mat, 0.9f);
            g3d_material_impl_set_metallic(mat, 0.0f);
        }
        int e = g3d_entity_impl_spawn(scene, 0, 0.0f, 0.0f, 0.0f);
        G3DEntity *E = g3d_entity_impl_get(e);
        if (!E) continue;
        E->mesh = (void *)mesh; E->model_id = 0;
        if (mat >= 0) g3d_entity_impl_set_material(e, mat);
        R->ent[su] = e; R->mat[su] = mat; made++;
    }
    return made;
}
