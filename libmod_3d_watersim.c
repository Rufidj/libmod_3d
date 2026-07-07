/*
 * libmod_3d_watersim.c - Unified height-field water: live shallow-water sim.
 *
 * Virtual-pipes model (Mei et al.): each terrain cell holds a water DEPTH and
 * four outflow fluxes to its neighbours. Each step, flux accelerates toward
 * lower total-height neighbours (terrain+water) under gravity, is clamped to the
 * available water, then depths update from net in/outflow. Water pools in basins
 * (lakes) and runs down slopes (rivers) as ONE continuous field. Map edges drain
 * (water leaves), so spring-fed rivers reach a thin steady state.
 */

#include "libmod_3d_watersim.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_water.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WSIM_MAX_SRC 256
#define WSIM_EPS 0.006f         /* below this depth a cell is "dry" (not drawn) */

static struct {
    int active;
    int side;
    float ws, cell_w;
    float *terr;                /* terrain copy (side*side) */
    float *d;                   /* water depth */
    float *fL, *fR, *fT, *fB;   /* outflow flux to left/right/top/bottom */
    float *vx, *vz;             /* velocity (flow direction for the shader) */
    struct { int cell; float rate; float x, z; } src[WSIM_MAX_SRC];
    int nsrc;
    float rain, sea, evap, flow_scale;
    G3DMesh *mesh;
    float accum;                /* fixed-timestep accumulator */
} W = {0};

static int wsim_cell(float x, float z) {
    int grid = W.side - 1;
    int i = (int)lrintf((x / W.ws + 0.5f) * grid);
    int j = (int)lrintf((z / W.ws + 0.5f) * grid);
    if (i < 0) i = 0; if (j < 0) j = 0;
    if (i > grid) i = grid; if (j > grid) j = grid;
    return j * W.side + i;
}

static void wsim_free_arrays(void) {
    free(W.terr); free(W.d);
    free(W.fL); free(W.fR); free(W.fT); free(W.fB);
    free(W.vx); free(W.vz);
    W.terr = W.d = W.fL = W.fR = W.fT = W.fB = W.vx = W.vz = NULL;
    if (W.mesh) { g3d_mesh_free(W.mesh); W.mesh = NULL; }
}

void g3d_watersim_init(const float *heights, int side, float world_size) {
    if (!heights || side < 2) return;
    wsim_free_arrays();
    int N = side * side;
    W.side = side; W.ws = world_size; W.cell_w = world_size / (float)(side - 1);
    W.terr = (float *)malloc((size_t)N * sizeof(float));
    memcpy(W.terr, heights, (size_t)N * sizeof(float));
    W.d  = (float *)calloc((size_t)N, sizeof(float));
    W.fL = (float *)calloc((size_t)N, sizeof(float));
    W.fR = (float *)calloc((size_t)N, sizeof(float));
    W.fT = (float *)calloc((size_t)N, sizeof(float));
    W.fB = (float *)calloc((size_t)N, sizeof(float));
    W.vx = (float *)calloc((size_t)N, sizeof(float));
    W.vz = (float *)calloc((size_t)N, sizeof(float));
    W.nsrc = 0; W.rain = 0.0f; W.sea = -1e30f;
    /* default evaporation bounds the water: the wider it spreads, the more it
       loses, so a constant spring settles to a finite lake+river instead of
       creeping across the whole map. 0 = unbounded (flooded-map mode). */
    W.evap = 0.04f;
    W.flow_scale = 1.0f; W.accum = 0.0f;
    W.active = 1;
}

void g3d_watersim_set_terrain(const float *heights) {
    if (!W.active || !heights) return;
    memcpy(W.terr, heights, (size_t)(W.side * W.side) * sizeof(float));
}

void g3d_watersim_shutdown(void) {
    wsim_free_arrays();
    W.active = 0; W.nsrc = 0;
}

int g3d_watersim_active(void) { return W.active; }

int g3d_watersim_add_source(float x, float z, float rate) {
    if (!W.active || W.nsrc >= WSIM_MAX_SRC) return -1;
    W.src[W.nsrc].cell = wsim_cell(x, z);
    W.src[W.nsrc].rate = rate;
    W.src[W.nsrc].x = x; W.src[W.nsrc].z = z;
    return W.nsrc++;
}
void g3d_watersim_clear_sources(void) { W.nsrc = 0; }
int  g3d_watersim_source_count(void) { return W.nsrc; }

int g3d_watersim_get_source(int i, float *x, float *z, float *rate) {
    if (i < 0 || i >= W.nsrc) return 0;
    if (x) *x = W.src[i].x; if (z) *z = W.src[i].z; if (rate) *rate = W.src[i].rate;
    return 1;
}
void g3d_watersim_get_params(float *rain, float *sea, float *evap, float *flow) {
    if (rain) *rain = W.rain; if (sea) *sea = W.sea;
    if (evap) *evap = W.evap; if (flow) *flow = W.flow_scale;
}
void g3d_watersim_set_rain(float rate) { W.rain = rate; }
void g3d_watersim_set_sea_level(float y) { W.sea = y; }
void g3d_watersim_set_evaporation(float rate) { W.evap = rate; }
void g3d_watersim_set_flow_scale(float s) { W.flow_scale = s < 0.0f ? 0.0f : s; }

const float *g3d_watersim_depth(void) { return W.d; }
int g3d_watersim_side(void) { return W.side; }

float g3d_watersim_level_at(float x, float z) {
    if (!W.active) return -1e30f;
    int c = wsim_cell(x, z);
    return (W.d[c] > WSIM_EPS) ? W.terr[c] + W.d[c] : -1e30f;
}

/* One physical sub-step of duration h (seconds). */
static void wsim_substep(float h) {
    int S = W.side, N = S * S;
    const float g = 9.81f;
    float A = W.cell_w, l = W.cell_w, area = W.cell_w * W.cell_w;
    float damp = W.flow_scale;

    /* inflow: sources, rain, sea; outflow: evaporation */
    for (int k = 0; k < W.nsrc; k++) {
        int c = W.src[k].cell;
        W.d[c] += W.src[k].rate * h;
        if (W.d[c] < 0.0f) W.d[c] = 0.0f;
    }
    if (W.rain > 0.0f) for (int c = 0; c < N; c++) W.d[c] += W.rain * h;
    if (W.sea > -1e29f) for (int c = 0; c < N; c++) {
        float need = W.sea - W.terr[c];
        if (need > W.d[c]) W.d[c] = need;
    }
    if (W.evap > 0.0f) { float e = W.evap * h; for (int c = 0; c < N; c++) { W.d[c] -= e; if (W.d[c] < 0.0f) W.d[c] = 0.0f; } }

    /* accelerate the four outflow pipes toward lower total-height neighbours;
       map-edge neighbours use flat terrain (so surface water drains off) */
    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
        int c = j * S + i;
        float hc = W.terr[c] + W.d[c];
        float sL = (i > 0)     ? (W.terr[c - 1] + W.d[c - 1]) : W.terr[c];
        float sR = (i < S - 1) ? (W.terr[c + 1] + W.d[c + 1]) : W.terr[c];
        float sT = (j > 0)     ? (W.terr[c - S] + W.d[c - S]) : W.terr[c];
        float sB = (j < S - 1) ? (W.terr[c + S] + W.d[c + S]) : W.terr[c];
        float nL = (W.fL[c] + h * g * A * (hc - sL) / l) * damp; if (nL < 0.0f) nL = 0.0f;
        float nR = (W.fR[c] + h * g * A * (hc - sR) / l) * damp; if (nR < 0.0f) nR = 0.0f;
        float nT = (W.fT[c] + h * g * A * (hc - sT) / l) * damp; if (nT < 0.0f) nT = 0.0f;
        float nB = (W.fB[c] + h * g * A * (hc - sB) / l) * damp; if (nB < 0.0f) nB = 0.0f;
        float tot = (nL + nR + nT + nB) * h;
        if (tot > W.d[c] * area && tot > 1e-9f) {
            float K = W.d[c] * area / tot;
            nL *= K; nR *= K; nT *= K; nB *= K;
        }
        W.fL[c] = nL; W.fR[c] = nR; W.fT[c] = nT; W.fB[c] = nB;
    }

    /* apply net flux to depths, and derive per-cell velocity for the shader */
    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
        int c = j * S + i;
        float in = 0.0f;
        if (i > 0)     in += W.fR[c - 1];
        if (i < S - 1) in += W.fL[c + 1];
        if (j > 0)     in += W.fB[c - S];
        if (j < S - 1) in += W.fT[c + S];
        float out = W.fL[c] + W.fR[c] + W.fT[c] + W.fB[c];
        W.d[c] += h * (in - out) / area;
        if (W.d[c] < 0.0f) W.d[c] = 0.0f;
        float fx = (((i > 0 ? W.fR[c - 1] : 0.0f) - W.fL[c]) + (W.fR[c] - (i < S - 1 ? W.fL[c + 1] : 0.0f))) * 0.5f;
        float fz = (((j > 0 ? W.fB[c - S] : 0.0f) - W.fT[c]) + (W.fB[c] - (j < S - 1 ? W.fT[c + S] : 0.0f))) * 0.5f;
        float dd = W.d[c] > 0.05f ? W.d[c] : 0.05f;
        W.vx[c] = fx / (W.cell_w * dd);
        W.vz[c] = fz / (W.cell_w * dd);
    }
}

void g3d_watersim_step(float dt) {
    if (!W.active) return;
    const float H = 0.02f;          /* fixed sub-step for stability */
    const int MAX_SUB = 4;          /* bound cost per frame */
    W.accum += dt;
    int n = 0;
    while (W.accum >= H && n < MAX_SUB) { wsim_substep(H); W.accum -= H; n++; }
    if (n == MAX_SUB) W.accum = 0.0f;   /* don't spiral if we fell behind */
}

void g3d_watersim_settle(float seconds) {
    if (!W.active || seconds <= 0.0f) return;
    const float H = 0.02f;
    int steps = (int)(seconds / H);
    if (steps > 4000) steps = 4000;    /* ~80s of sim; plenty to reach steady state */
    for (int i = 0; i < steps; i++) wsim_substep(H);
}

/* Build (or rebuild) the surface mesh from the current water field: one quad per
   watered cell at terrain+depth, flow direction packed in normal.xz and depth in
   texcoord.x (same layout the shared water shader expects). */
static void wsim_build_mesh(void) {
    if (W.mesh) { g3d_mesh_free(W.mesh); W.mesh = NULL; }
    int S = W.side, grid = S - 1, N = S * S;

    /* Per-vertex WATER SURFACE LEVEL = average of nearby WET cells' (terrain+depth).
       Crucial: a shore vertex sits at the water level of the adjacent water, NOT at
       its own high terrain + a sliver of depth (which produced the jagged white flaps
       at the edges). The terrain mesh, being higher and opaque at the shore, hides the
       submerged rim, so the water reads as a clean flat sheet ending at the coast.
       Averaging also smooths the surface. `wet` marks vertices touching water. */
    float *wl = (float *)malloc((size_t)N * sizeof(float));
    char *wet = (char *)malloc((size_t)N);
    if (!wl || !wet) { free(wl); free(wet); return; }
    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
        int c = j * S + i; float sum = 0.0f; int cnt = 0;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
            int ni = i + di, nj = j + dj;
            if (ni < 0 || nj < 0 || ni >= S || nj >= S) continue;
            int nc = nj * S + ni;
            if (W.d[nc] > WSIM_EPS) { sum += W.terr[nc] + W.d[nc]; cnt++; }
        }
        if (cnt > 0) { wl[c] = sum / (float)cnt; wet[c] = 1; }
        else         { wl[c] = W.terr[c];        wet[c] = 0; }
    }

    /* draw a cell if any of its 4 corner vertices touches water (feathers the shore
       one cell so the sheet meets the coast cleanly) */
    int cells = 0;
    for (int j = 0; j < grid; j++) for (int i = 0; i < grid; i++)
        if (wet[j * S + i] || wet[j * S + i + 1] || wet[(j + 1) * S + i] || wet[(j + 1) * S + i + 1])
            cells++;
    if (cells == 0) { free(wl); free(wet); return; }

    G3DVertex *verts = (G3DVertex *)malloc((size_t)cells * 4 * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)cells * 6 * sizeof(uint32_t));
    if (!verts || !idx) { free(verts); free(idx); free(wl); free(wet); return; }
    int v = 0, ic = 0;
    float cw = W.cell_w;
    for (int j = 0; j < grid; j++) {
        for (int i = 0; i < grid; i++) {
            if (!(wet[j * S + i] || wet[j * S + i + 1] || wet[(j + 1) * S + i] || wet[(j + 1) * S + i + 1]))
                continue;
            float x0 = ((float)i / grid - 0.5f) * W.ws;
            float z0 = ((float)j / grid - 0.5f) * W.ws;
            int ci[4] = { i, i + 1, i, i + 1 };
            int cj[4] = { j, j, j + 1, j + 1 };
            float cx[4] = { x0, x0 + cw, x0, x0 + cw };
            float cz[4] = { z0, z0, z0 + cw, z0 + cw };
            int base = v;
            for (int k = 0; k < 4; k++) {
                int ii = ci[k], jj = cj[k];
                int cc = jj * S + ii;
                float lvl = wl[cc];                       /* water surface here */
                float dep = lvl - W.terr[cc]; if (dep < 0.0f) dep = 0.0f;
                verts[v].position[0] = cx[k];
                verts[v].position[1] = lvl;               /* flat at the water level */
                verts[v].position[2] = cz[k];
                float fx = W.vx[cc], fz = W.vz[cc];
                float fl = sqrtf(fx * fx + fz * fz);
                if (fl > 0.001f) { fx /= fl; fz /= fl; } else { fx = fz = 0.0f; }
                verts[v].normal[0] = fx; verts[v].normal[1] = 0.0f; verts[v].normal[2] = fz;
                verts[v].texcoord[0] = dep;               /* shore depth -> foam on shallow edges */
                verts[v].texcoord[1] = 0.0f;
                v++;
            }
            idx[ic++] = base + 0; idx[ic++] = base + 2; idx[ic++] = base + 1;
            idx[ic++] = base + 1; idx[ic++] = base + 2; idx[ic++] = base + 3;
        }
    }
    W.mesh = g3d_mesh_create("watersim", verts, (uint32_t)(cells * 4), idx, (uint32_t)(cells * 6));
    free(verts); free(idx); free(wl); free(wet);
    if (W.mesh) g3d_mesh_upload_gpu(W.mesh);
}

void g3d_watersim_render(G3DCamera *camera, int flip_y) {
    if (!W.active || !camera) return;
    wsim_build_mesh();
    if (W.mesh) g3d_water_draw_mesh(W.mesh, camera, flip_y);
}
