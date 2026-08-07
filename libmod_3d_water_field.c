/*
 * libmod_3d_water_field.c - The unified water field (shallow-water simulation)
 *
 * Virtual-pipes shallow water (Mei et al. 2007): every cell holds a water depth
 * and four outflow fluxes. Each sub-step the fluxes accelerate toward
 * lower-total-height neighbours under gravity, get clamped so a cell can never
 * push out more water than it holds, and then depths follow from the net
 * in/outflow. Water pools in basins and runs down slopes as one continuous
 * field, so lakes and rivers are the same thing seen at different slopes.
 *
 * Three things this does differently from a textbook implementation, each
 * fixing a concrete artifact:
 *
 *  - The routing surface is smoothed, the REAL terrain is not. Terrain noise
 *    makes micro-pits that trap water and stop rivers flowing; routing over a
 *    smoothed copy lets water move, while depths and surface levels stay tied
 *    to the true ground, so water never floats above the terrain.
 *  - The sim only walks the wet bounding box, not the whole grid. A river on a
 *    1024^2 map touches a few percent of the cells.
 *  - The sub-step is chosen from the CFL condition instead of being fixed, so
 *    deep water stays stable and shallow water does not waste steps.
 */

#include "libmod_3d_water_field.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WF_MAX_SPRINGS 256
#define WF_WET_EPS     0.004f    /* below this depth a cell counts as dry */
#define WF_GRAVITY     9.81f

typedef struct { int cell; float x, z, rate; int tag; } WFSpring;

static struct {
    int   active;
    int   side;
    float world_size;
    float cell_w;
    float inv_cell_w;

    float *terr;      /* exact terrain, sets levels and depths                */
    float *route;     /* smoothed copy, used only to route the flow           */
    float *d;         /* water depth per cell                                 */
    float *fL, *fR, *fT, *fB;   /* outflow flux to each neighbour             */
    float *vx, *vz;   /* horizontal velocity, units/second                    */
    float *hold;      /* surface each cell must keep (placed water); NULL = none  */
    unsigned char *obst;  /* 1 = solid: a rock stands here and water goes round it */

    WFSpring springs[WF_MAX_SPRINGS];
    int   nspring;

    float rain, sea, evap, visc;
    int   route_smooth;
    float route_tol;         /* how far routing may deviate from real ground */

    /* inclusive bounding box of the wet region, in cells */
    int   bi0, bj0, bi1, bj1;
    int   any_wet;
    int   sea_dirty;         /* sea level or terrain changed -> one full sweep */

    float accum;             /* leftover time between frames */
    unsigned int revision;
} W;

/* Revision numbers come from here and NEVER restart, not even when the field is
   torn down and rebuilt. Consumers (the GPU field texture, the waterfall sheets)
   cache by revision, and a counter that restarts at 1 can land on the number a
   consumer already has -- so a brand new field is mistaken for the old one and
   nothing updates. Costs one global; removes a whole class of stale-cache bugs
   that only appear when a scene is reloaded. */
static unsigned int g_wf_revision_seq = 1;

/* --------------------------------------------------------------------------
   Grid helpers
   -------------------------------------------------------------------------- */

/* World (x,z) -> continuous grid coordinates. The grid spans world_size units
   centred on the origin, with sample 0 at -world_size/2 and sample side-1 at
   +world_size/2. */
static void wf_grid_coords(float x, float z, float *gi, float *gj) {
    int grid = W.side - 1;
    *gi = (x / W.world_size + 0.5f) * (float)grid;
    *gj = (z / W.world_size + 0.5f) * (float)grid;
}

static int wf_cell_of(float x, float z) {
    float gi, gj;
    wf_grid_coords(x, z, &gi, &gj);
    int i = (int)lrintf(gi), j = (int)lrintf(gj);
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i > W.side - 1) i = W.side - 1;
    if (j > W.side - 1) j = W.side - 1;
    return j * W.side + i;
}

/* Corner indices + bilinear weights for a world position. Returns 0 if the
   point falls outside the field. */
static int wf_bilinear_setup(float x, float z, int c[4], float w[4]) {
    float gi, gj;
    wf_grid_coords(x, z, &gi, &gj);
    if (gi < 0.0f || gj < 0.0f || gi > (float)(W.side - 1) || gj > (float)(W.side - 1))
        return 0;
    int i0 = (int)floorf(gi), j0 = (int)floorf(gj);
    if (i0 > W.side - 2) i0 = W.side - 2;
    if (j0 > W.side - 2) j0 = W.side - 2;
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    float fx = gi - (float)i0, fz = gj - (float)j0;
    if (fx < 0.0f) fx = 0.0f; else if (fx > 1.0f) fx = 1.0f;
    if (fz < 0.0f) fz = 0.0f; else if (fz > 1.0f) fz = 1.0f;
    int S = W.side;
    c[0] = j0 * S + i0;         w[0] = (1.0f - fx) * (1.0f - fz);
    c[1] = j0 * S + i0 + 1;     w[1] = fx * (1.0f - fz);
    c[2] = (j0 + 1) * S + i0;   w[2] = (1.0f - fx) * fz;
    c[3] = (j0 + 1) * S + i0 + 1; w[3] = fx * fz;
    return 1;
}

/* --------------------------------------------------------------------------
   Setup
   -------------------------------------------------------------------------- */

/* Rebuild the routing surface: a smoothed copy of the terrain, then clamped to
   stay within `route_tol` of the real ground.
 *
 * The clamp is the whole point. Smoothing alone fixes the problem it is meant
 * to fix -- noise-scale pits that trap water and stop a river flowing -- but it
 * also raises the floor of GENUINE basins, and a basin whose routing floor sits
 * above its real floor stops accepting water early, leaving a dish-shaped dent
 * in what should be a flat lake surface. Bounding the shift to the noise scale
 * keeps the micro-pit fix and limits the level error everywhere to route_tol.
 * The lower clamp matters just as much: unclamped smoothing shaves ridges, and
 * a shaved ridge leaks water over a barrier that should hold it. */
static void wf_rebuild_routing(void) {
    int S = W.side, N = S * S;
    memcpy(W.route, W.terr, (size_t)N * sizeof(float));
    if (W.route_smooth <= 0 || S < 3) return;

    float *tmp = (float *)malloc((size_t)N * sizeof(float));
    if (!tmp) return;
    for (int p = 0; p < W.route_smooth; p++) {
        for (int j = 0; j < S; j++) {
            for (int i = 0; i < S; i++) {
                float sum = 0.0f;
                int cnt = 0;
                for (int dj = -1; dj <= 1; dj++) {
                    int nj = j + dj;
                    if (nj < 0 || nj >= S) continue;
                    for (int di = -1; di <= 1; di++) {
                        int ni = i + di;
                        if (ni < 0 || ni >= S) continue;
                        sum += W.route[nj * S + ni];
                        cnt++;
                    }
                }
                tmp[j * S + i] = sum / (float)cnt;
            }
        }
        memcpy(W.route, tmp, (size_t)N * sizeof(float));
    }
    free(tmp);

    float tol = W.route_tol;
    for (int c = 0; c < N; c++) {
        float lo = W.terr[c] - tol, hi = W.terr[c] + tol;
        if (W.route[c] < lo) W.route[c] = lo;
        else if (W.route[c] > hi) W.route[c] = hi;
    }
}

static void wf_free_arrays(void) {
    free(W.terr); free(W.route); free(W.d);
    free(W.fL); free(W.fR); free(W.fT); free(W.fB);
    free(W.vx); free(W.vz); free(W.hold); free(W.obst);
    W.hold = NULL; W.obst = NULL;
    W.terr = W.route = W.d = NULL;
    W.fL = W.fR = W.fT = W.fB = NULL;
    W.vx = W.vz = NULL;
}

int g3d_waterfield_init(const float *terrain, int side, float world_size) {
    if (!terrain || side < 2 || world_size <= 0.0f) return 0;
    wf_free_arrays();
    memset(&W, 0, sizeof(W));

    int N = side * side;
    W.side = side;
    W.world_size = world_size;
    W.cell_w = world_size / (float)(side - 1);
    W.inv_cell_w = 1.0f / W.cell_w;

    W.terr  = (float *)malloc((size_t)N * sizeof(float));
    W.route = (float *)malloc((size_t)N * sizeof(float));
    W.d  = (float *)calloc((size_t)N, sizeof(float));
    W.fL = (float *)calloc((size_t)N, sizeof(float));
    W.fR = (float *)calloc((size_t)N, sizeof(float));
    W.fT = (float *)calloc((size_t)N, sizeof(float));
    W.fB = (float *)calloc((size_t)N, sizeof(float));
    W.vx = (float *)calloc((size_t)N, sizeof(float));
    W.vz = (float *)calloc((size_t)N, sizeof(float));
    if (!W.terr || !W.route || !W.d || !W.fL || !W.fR || !W.fT || !W.fB || !W.vx || !W.vz) {
        wf_free_arrays();
        memset(&W, 0, sizeof(W));
        return 0;
    }
    memcpy(W.terr, terrain, (size_t)N * sizeof(float));

    W.sea = G3D_NO_WATER;
    W.rain = 0.0f;
    /* A little evaporation by default bounds a constant spring to a finite lake
       and river; with none, water spreads until it reaches a map edge. */
    W.evap = 0.04f;
    W.visc = 1.0f;
    W.route_smooth = 1;
    W.route_tol = 0.25f;
    wf_rebuild_routing();

    W.any_wet = 0;
    W.sea_dirty = 1;
    W.bi0 = W.bj0 = 0;
    W.bi1 = W.bj1 = side - 1;
    W.revision = ++g_wf_revision_seq;
    W.active = 1;
    return 1;
}

void g3d_waterfield_set_terrain(const float *terrain) {
    if (!W.active || !terrain) return;
    /* Depths are kept: raising ground under a lake displaces the water rather
       than deleting it, and the sim redistributes it over the next steps. */
    memcpy(W.terr, terrain, (size_t)(W.side * W.side) * sizeof(float));
    wf_rebuild_routing();
    W.sea_dirty = 1;   /* the sea must re-claim cells the edit lowered */
    W.revision = ++g_wf_revision_seq;
}

void g3d_waterfield_shutdown(void) {
    wf_free_arrays();
    memset(&W, 0, sizeof(W));
}

int   g3d_waterfield_active(void)     { return W.active; }
int   g3d_waterfield_side(void)       { return W.side; }
float g3d_waterfield_world_size(void) { return W.world_size; }
const float *g3d_waterfield_depth_array(void)   { return W.d; }
const float *g3d_waterfield_terrain_array(void) { return W.terr; }

void g3d_waterfield_flow_arrays(const float **vx, const float **vz) {
    if (vx) *vx = W.vx;
    if (vz) *vz = W.vz;
}
unsigned int g3d_waterfield_revision(void)      { return W.revision; }

/* --------------------------------------------------------------------------
   Sources
   -------------------------------------------------------------------------- */

static void wf_touch_bounds(int i, int j);

int g3d_waterfield_add_spring_tagged(float x, float z, float rate, int tag) {
    if (!W.active || W.nspring >= WF_MAX_SPRINGS) return -1;
    WFSpring *s = &W.springs[W.nspring];
    s->cell = wf_cell_of(x, z);
    s->x = x; s->z = z; s->rate = rate; s->tag = tag;

    /* El rectangulo mojado tiene que cubrir el manantial DESDE YA. La simulacion
       solo recorre ese rectangulo, asi que un manantial fuera de el vierte en
       una celda que nunca se visita: el agua se apila en el sitio y no baja.
       Pasaba solo desde un juego, donde el mar se asienta antes de crear los
       manantiales y deja el rectangulo pegado a la costa, con la cabecera del
       rio fuera. */
    wf_touch_bounds(s->cell % W.side, s->cell / W.side);
    return W.nspring++;
}

int g3d_waterfield_add_spring(float x, float z, float rate) {
    return g3d_waterfield_add_spring_tagged(x, z, rate, 0);
}

void g3d_waterfield_clear_springs_tagged(int tag) {
    int n = 0;
    for (int i = 0; i < W.nspring; i++)
        if (W.springs[i].tag != tag) W.springs[n++] = W.springs[i];
    W.nspring = n;
}

void g3d_waterfield_clear_springs(void) { W.nspring = 0; }
int  g3d_waterfield_spring_count(void)  { return W.nspring; }

int g3d_waterfield_get_spring(int i, float *x, float *z, float *rate) {
    if (i < 0 || i >= W.nspring) return 0;
    if (x)    *x    = W.springs[i].x;
    if (z)    *z    = W.springs[i].z;
    if (rate) *rate = W.springs[i].rate;
    return 1;
}

void g3d_waterfield_set_sea_level(float y) {
    /* Only a real change forces the full-grid sweep. Callers re-assert the sea
       level every frame, and marking it dirty each time would make every
       sub-step scan the whole map instead of just the wet region. */
    if (y == W.sea) return;
    float old = W.sea;
    W.sea = y;
    W.sea_dirty = 1;

    /* Tide going out: take back the water the sea itself had placed on ground
       that it no longer covers.
     *
     * Left to the simulation this is correct but slow -- the stranded water has
     * to physically run downhill, which takes tens of seconds of sim time and
     * looks like a plateau of water sitting above the new surface. Since the sea
     * put that water there, the sea can remove it, and lowering the level then
     * responds immediately. Ground the sea never reached is untouched, so
     * spring-fed and rain-fed lakes above the old shoreline survive. */
    if (W.active && old > G3D_NO_WATER_TEST && y < old) {
        int N = W.side * W.side;
        for (int c = 0; c < N; c++)
            if (W.terr[c] >= y && W.terr[c] < old)
                W.d[c] = 0.0f;
    }
    W.revision = ++g_wf_revision_seq;
}
float g3d_waterfield_get_sea_level(void)      { return W.sea; }
void g3d_waterfield_set_rain(float rate)      { W.rain = rate > 0.0f ? rate : 0.0f; }
void g3d_waterfield_set_evaporation(float r)  { W.evap = r > 0.0f ? r : 0.0f; }
void g3d_waterfield_set_viscosity(float s)    { W.visc = s < 0.0f ? 0.0f : s; }

void g3d_waterfield_set_routing(int passes, float tolerance) {
    if (passes < 0) passes = 0;
    if (passes > 8) passes = 8;
    if (tolerance < 0.0f) tolerance = 0.0f;
    if (passes == W.route_smooth && tolerance == W.route_tol) return;
    W.route_smooth = passes;
    W.route_tol = tolerance;
    if (W.active) { wf_rebuild_routing(); W.revision = ++g_wf_revision_seq; }
}

void g3d_waterfield_get_params(float *rain, float *sea, float *evap, float *visc) {
    if (rain) *rain = W.rain;
    if (sea)  *sea  = W.sea;
    if (evap) *evap = W.evap;
    if (visc) *visc = W.visc;
}

/* --------------------------------------------------------------------------
   Authoring
   -------------------------------------------------------------------------- */

/* Mark every cell as wet so the next sub-step sees the newly placed water. */
static void wf_touch_bounds(int i, int j) {
    if (!W.any_wet) { W.bi0 = W.bi1 = i; W.bj0 = W.bj1 = j; W.any_wet = 1; return; }
    if (i < W.bi0) W.bi0 = i;
    if (i > W.bi1) W.bi1 = i;
    if (j < W.bj0) W.bj0 = j;
    if (j > W.bj1) W.bj1 = j;
}

/* Vacia el agua del campo dejando el terreno. Sin esto, quien recompone el agua
   (el editor al mover un lago, al borrarlo, o al previsualizar) solo podia
   ANADIR: el agua de la version anterior se quedaba encima y el mapa se
   inundaba solo con pasar el raton. */
void g3d_waterfield_clear_water(void) {
    if (!W.active) return;
    int N = W.side * W.side;
    if (W.d)  memset(W.d,  0, (size_t)N * sizeof(float));
    if (W.fL) memset(W.fL, 0, (size_t)N * sizeof(float));
    if (W.fR) memset(W.fR, 0, (size_t)N * sizeof(float));
    if (W.fT) memset(W.fT, 0, (size_t)N * sizeof(float));
    if (W.fB) memset(W.fB, 0, (size_t)N * sizeof(float));
    if (W.vx) memset(W.vx, 0, (size_t)N * sizeof(float));
    if (W.vz) memset(W.vz, 0, (size_t)N * sizeof(float));
    if (W.hold) { free(W.hold); W.hold = NULL; }
    W.any_wet = 0;
    W.bi0 = W.bj0 = 0; W.bi1 = W.bj1 = W.side - 1;
    W.sea_dirty = 1;          /* el mar vuelve a reclamar lo suyo */
    W.revision = ++g_wf_revision_seq;
}

void g3d_waterfield_clear_holds(void) {
    if (W.hold) { free(W.hold); W.hold = NULL; W.revision = ++g_wf_revision_seq; }
}

/* Remember that this cell must keep at least `level` of surface. */
static void wf_hold_cell(int c, float level) {
    if (!W.hold) {
        int N = W.side * W.side;
        W.hold = (float *)malloc((size_t)N * sizeof(float));
        if (!W.hold) return;
        for (int k = 0; k < N; k++) W.hold[k] = G3D_NO_WATER;
    }
    if (level > W.hold[c]) W.hold[c] = level;
}

int g3d_waterfield_fill_basin(float x, float z, float level, float max_radius) {
    if (!W.active) return 0;
    int S = W.side, N = S * S;
    int seed = wf_cell_of(x, z);
    if (W.terr[seed] >= level) return 0;      /* the seed is already above water */

    char *seen = (char *)calloc((size_t)N, 1);
    int *queue = (int *)malloc((size_t)N * sizeof(int));
    if (!seen || !queue) { free(seen); free(queue); return 0; }

    /* Breadth-first over connected cells below the surface. Flood fill, not a
       height test over the whole map: water poured here must not appear in an
       unrelated basin that happens to sit at the same altitude. */
    int head = 0, tail = 0, filled = 0;
    queue[tail++] = seed;
    seen[seed] = 1;
    float r2 = max_radius > 0.0f ? max_radius * max_radius : 0.0f;
    int si = seed % S, sj = seed / S;

    while (head < tail) {
        int c = queue[head++];
        int i = c % S, j = c / S;
        float need = level - W.terr[c];
        if (need <= 0.0f) continue;
        if (need > W.d[c]) W.d[c] = need;
        wf_hold_cell(c, level);
        wf_touch_bounds(i, j);
        filled++;

        const int di[4] = { -1, 1, 0, 0 }, dj[4] = { 0, 0, -1, 1 };
        for (int k = 0; k < 4; k++) {
            int ni = i + di[k], nj = j + dj[k];
            if (ni < 0 || nj < 0 || ni >= S || nj >= S) continue;
            int nc = nj * S + ni;
            if (seen[nc] || W.terr[nc] >= level) continue;
            if (r2 > 0.0f) {
                float dx = (float)(ni - si) * W.cell_w, dz = (float)(nj - sj) * W.cell_w;
                if (dx * dx + dz * dz > r2) continue;
            }
            seen[nc] = 1;
            queue[tail++] = nc;
        }
    }
    free(seen); free(queue);
    if (filled) W.revision = ++g_wf_revision_seq;
    return filled;
}

int g3d_waterfield_add_channel(const float *pts_xyz, int n, float width) {
    if (!W.active || !pts_xyz || n < 2) return 0;
    int S = W.side;
    float half = width * 0.5f;
    if (half < W.cell_w * 0.5f) half = W.cell_w * 0.5f;
    int filled = 0;

    for (int s = 0; s < n - 1; s++) {
        const float *a = &pts_xyz[s * 3], *b = &pts_xyz[(s + 1) * 3];
        float ax = a[0], az = a[2], bx = b[0], bz = b[2];
        float dx = bx - ax, dz = bz - az;
        float len2 = dx * dx + dz * dz;
        if (len2 < 1e-8f) continue;

        /* Cell range covering this segment's swept rectangle. */
        float minx = (ax < bx ? ax : bx) - half, maxx = (ax > bx ? ax : bx) + half;
        float minz = (az < bz ? az : bz) - half, maxz = (az > bz ? az : bz) + half;
        float gi0, gj0, gi1, gj1;
        wf_grid_coords(minx, minz, &gi0, &gj0);
        wf_grid_coords(maxx, maxz, &gi1, &gj1);
        int i0 = (int)floorf(gi0), i1 = (int)ceilf(gi1);
        int j0 = (int)floorf(gj0), j1 = (int)ceilf(gj1);
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > S - 1) i1 = S - 1;
        if (j1 > S - 1) j1 = S - 1;

        for (int j = j0; j <= j1; j++) {
            for (int i = i0; i <= i1; i++) {
                float wxp = ((float)i / (float)(S - 1) - 0.5f) * W.world_size;
                float wzp = ((float)j / (float)(S - 1) - 0.5f) * W.world_size;
                /* Distance to the segment, and how far along it we are: the
                   surface height is interpolated from the endpoints so the
                   course can descend along its length. */
                float t = ((wxp - ax) * dx + (wzp - az) * dz) / len2;
                if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
                float px = ax + dx * t, pz = az + dz * t;
                float ex = wxp - px, ez = wzp - pz;
                if (ex * ex + ez * ez > half * half) continue;

                int c = j * S + i;
                float surf = a[1] + (b[1] - a[1]) * t;
                float need = surf - W.terr[c];
                if (need <= 0.0f) continue;
                if (need > W.d[c]) W.d[c] = need;
                wf_hold_cell(c, surf);
                wf_touch_bounds(i, j);
                filled++;
            }
        }
    }
    if (filled) W.revision = ++g_wf_revision_seq;
    return filled;
}

/* --------------------------------------------------------------------------
   Simulation
   -------------------------------------------------------------------------- */

/* Widen the active box by one ring and clamp it to the grid, so water can
   advance into cells that were dry last step. */
static void wf_expand_bounds(void) {
    if (--W.bi0 < 0) W.bi0 = 0;
    if (--W.bj0 < 0) W.bj0 = 0;
    if (++W.bi1 > W.side - 1) W.bi1 = W.side - 1;
    if (++W.bj1 > W.side - 1) W.bj1 = W.side - 1;
}

/* One physical sub-step of `h` seconds over the active box. Returns the maximum
   depth seen, which sets the next step's CFL limit. */
static float wf_substep(float h) {
    int S = W.side;
    const float area = W.cell_w * W.cell_w;
    const float damp = W.visc;

    /* Sources first: springs, rain and the sea, minus evaporation. These can
       wet new cells, so they run over the expanded box. */
    wf_expand_bounds();

    for (int k = 0; k < W.nspring; k++) {
        int c = W.springs[k].cell;
        W.d[c] += W.springs[k].rate * h;
        if (W.d[c] < 0.0f) W.d[c] = 0.0f;
    }
    int has_sea = (W.sea > G3D_NO_WATER_TEST);
    if (W.rain > 0.0f || W.evap > 0.0f || has_sea || W.hold) {
        float add = W.rain * h, ev = W.evap * h;
        /* Normally the active box is enough. Two cases need a full sweep: rain
           wets every cell by definition, and the sea must claim its cells once
           after the level or the terrain changes. Afterwards sea cells stay wet
           and therefore stay inside the box, so the sweep is not repeated. */
        int full = W.sea_dirty || (W.rain > 0.0f);
        int i0 = W.bi0, j0 = W.bj0, i1 = W.bi1, j1 = W.bj1;
        if (full) { i0 = 0; j0 = 0; i1 = S - 1; j1 = S - 1; W.sea_dirty = 0; }
        for (int j = j0; j <= j1; j++) {
            for (int i = i0; i <= i1; i++) {
                int c = j * S + i;
                /* Placed water is exempt from evaporation ONLY DOWN TO the level
                   it was placed at; anything above that evaporates normally.
                 *
                 * Both simpler rules are wrong. Refilling to the level makes an
                 * authored lake an inexhaustible spring and floods the map.
                 * Exempting the cell outright looks fine until something feeds
                 * it -- a river's head spring pours into cells that then have no
                 * way to lose water at all, so it creeps up for as long as the
                 * game runs. Evaporating only the excess leaves both a lake that
                 * cannot dry up and a fed channel that reaches equilibrium. */
                float dep = W.d[c] + add;
                float floor_d = 0.0f;
                if (W.hold && W.hold[c] > G3D_NO_WATER_TEST) {
                    floor_d = W.hold[c] - W.terr[c];
                    if (floor_d < 0.0f) floor_d = 0.0f;
                }
                float loss = ev;
                if (dep - loss < floor_d) loss = dep - floor_d;
                if (loss > 0.0f) dep -= loss;
                if (dep < 0.0f) dep = 0.0f;
                if (has_sea) {
                    /* The sea is a boundary condition, so it PINS the depth of
                       every cell it covers instead of only topping it up. Filling
                       one way only meant lowering the sea left the water it had
                       already placed stranded at the old height -- a cliff of
                       water standing over the new surface, which is what dragging
                       a level slider in the editor produced. Ground above the sea
                       is untouched, so springs and rain still make their own
                       lakes up there. */
                    float need = W.sea - W.terr[c];
                    if (need > 0.0f) {
                        /* ...pero el agua COLOCADA manda sobre el mar. Un lago
                           puesto a mano sobre una cuenca cuyo fondo queda bajo el
                           nivel del mar es una decision del autor, no un charco
                           que el mar deba nivelar. Sin esto, en el editor (sin
                           mar) el lago se veia y en el juego (que si crea mar)
                           desaparecia: el pin lo bajaba al nivel del mar en el
                           primer asentado. */
                        float keep = 0.0f;
                        if (W.hold && W.hold[c] > G3D_NO_WATER_TEST)
                            keep = W.hold[c] - W.terr[c];
                        dep = (need > keep) ? need : keep;
                    }
                }
                W.d[c] = dep;
            }
        }
        if (full) { W.bi0 = 0; W.bj0 = 0; W.bi1 = S - 1; W.bj1 = S - 1; }
    }

    /* Accelerate the four outflow pipes toward lower total-height neighbours.
       Total height uses the ROUTING terrain so noise pits do not trap water;
       off-grid neighbours use the cell's own terrain, which drains the edges. */
    for (int j = W.bj0; j <= W.bj1; j++) {
        for (int i = W.bi0; i <= W.bi1; i++) {
            int c = j * S + i;
            float dc = W.d[c];
            if (dc <= 0.0f) { W.fL[c] = W.fR[c] = W.fT[c] = W.fB[c] = 0.0f; continue; }
            /* Nothing flows out of solid rock. */
            if (W.obst && W.obst[c]) { W.fL[c] = W.fR[c] = W.fT[c] = W.fB[c] = 0.0f; continue; }

            float hc = W.route[c] + dc;
            float sL = (i > 0)     ? (W.route[c - 1] + W.d[c - 1]) : W.route[c];
            float sR = (i < S - 1) ? (W.route[c + 1] + W.d[c + 1]) : W.route[c];
            float sT = (j > 0)     ? (W.route[c - S] + W.d[c - S]) : W.route[c];
            float sB = (j < S - 1) ? (W.route[c + S] + W.d[c + S]) : W.route[c];

            float acc = h * WF_GRAVITY;
            float nL = (W.fL[c] + acc * (hc - sL)) * damp; if (nL < 0.0f) nL = 0.0f;
            float nR = (W.fR[c] + acc * (hc - sR)) * damp; if (nR < 0.0f) nR = 0.0f;
            float nT = (W.fT[c] + acc * (hc - sT)) * damp; if (nT < 0.0f) nT = 0.0f;
            float nB = (W.fB[c] + acc * (hc - sB)) * damp; if (nB < 0.0f) nB = 0.0f;

            /* ...and nothing flows INTO it either. Closing these four pipes is
               the whole trick: the water arriving at a rock has nowhere forward
               to go, so it piles up on the upstream face and leaves sideways --
               which is what going round something actually is. */
            if (W.obst) {
                if (i > 0     && W.obst[c - 1]) nL = 0.0f;
                if (i < S - 1 && W.obst[c + 1]) nR = 0.0f;
                if (j > 0     && W.obst[c - S]) nT = 0.0f;
                if (j < S - 1 && W.obst[c + S]) nB = 0.0f;
            }

            /* A cell may never push out more water than it holds -- and where an
               author placed water, it may only give away the EXCESS above that.
             *
               This is what makes hand-placed lakes and rivers stable without
               either drying up or inventing water. A placed body keeps exactly
               the surface it was given: it cannot drain, because it will not
               hand over its own volume, and it cannot flood, because it never
               produces any. Rain, springs and the sea still pile water on top,
               and THAT surplus flows away normally -- so a lake still overflows
               its rim when something fills it past the brim. */
            float avail = dc;
            if (W.hold && W.hold[c] > G3D_NO_WATER_TEST) {
                float keep = W.hold[c] - W.terr[c];
                if (keep > 0.0f) avail = dc - keep;
                if (avail < 0.0f) avail = 0.0f;
            }
            float out = (nL + nR + nT + nB) * h;
            float have = avail * area;
            if (out > have && out > 1e-9f) {
                float k = have / out;
                nL *= k; nR *= k; nT *= k; nB *= k;
            }
            W.fL[c] = nL; W.fR[c] = nR; W.fT[c] = nT; W.fB[c] = nB;
        }
    }

    /* Apply the net flux to the depths and derive per-cell velocity, tracking
       the new wet bounds and the deepest water as we go. */
    int ni0 = S, nj0 = S, ni1 = -1, nj1 = -1;
    float maxd = 0.0f;
    for (int j = W.bj0; j <= W.bj1; j++) {
        for (int i = W.bi0; i <= W.bi1; i++) {
            int c = j * S + i;
            float in = 0.0f;
            if (i > 0)     in += W.fR[c - 1];
            if (i < S - 1) in += W.fL[c + 1];
            if (j > 0)     in += W.fB[c - S];
            if (j < S - 1) in += W.fT[c + S];
            float out = W.fL[c] + W.fR[c] + W.fT[c] + W.fB[c];

            float dep = W.d[c] + h * (in - out) / area;
            if (dep < 0.0f) dep = 0.0f;
            W.d[c] = dep;

            if (dep > WF_WET_EPS) {
                if (i < ni0) ni0 = i;
                if (j < nj0) nj0 = j;
                if (i > ni1) ni1 = i;
                if (j > nj1) nj1 = j;
                if (dep > maxd) maxd = dep;

                /* Velocity from the average flux through the cell's faces. */
                float fx = ((i > 0     ? W.fR[c - 1] : 0.0f) - W.fL[c]
                          + W.fR[c] - (i < S - 1 ? W.fL[c + 1] : 0.0f)) * 0.5f;
                float fz = ((j > 0     ? W.fB[c - S] : 0.0f) - W.fT[c]
                          + W.fB[c] - (j < S - 1 ? W.fT[c + S] : 0.0f)) * 0.5f;
                float dd = dep > 0.05f ? dep : 0.05f;
                float ux = fx / (W.cell_w * dd);
                float uz = fz / (W.cell_w * dd);

                /* Cap the speed by the Froude number.
                 *
                 * The virtual-pipes model has no friction, so discharge over a
                 * thin film comes out as an enormous velocity -- measured 30
                 * units/s on an ordinary river, about 108 km/h. Real channel
                 * flow is limited by the bed instead: even a fast mountain
                 * stream stays near Fr = 1..2, where the speed cannot much
                 * exceed the shallow-water wave speed sqrt(g*h). Capping there
                 * costs one square root and keeps the flow map, the drift on
                 * floating objects and the ripple scroll all believable. The
                 * DEPTHS are untouched, so this changes how the water looks and
                 * pushes, never where it goes. */
                float speed = sqrtf(ux * ux + uz * uz);
                float vmax = 2.0f * sqrtf(WF_GRAVITY * dd);
                if (speed > vmax && speed > 1e-6f) {
                    float k = vmax / speed;
                    ux *= k; uz *= k;
                }
                W.vx[c] = ux;
                W.vz[c] = uz;
            } else {
                W.vx[c] = W.vz[c] = 0.0f;
            }
        }
    }

    if (ni1 < 0) {
        W.any_wet = 0;
        W.bi0 = W.bj0 = 0;
        W.bi1 = W.bj1 = S - 1;
    } else {
        W.any_wet = 1;
        W.bi0 = ni0; W.bj0 = nj0; W.bi1 = ni1; W.bj1 = nj1;
    }
    return maxd;
}

/* Largest stable sub-step for the current water, from the shallow-water CFL
   condition dt <= cell / (2*sqrt(g*h)): deep water carries fast gravity waves
   and needs finer steps, shallow water does not. */
static float wf_stable_step(float maxd) {
    if (maxd < 0.01f) maxd = 0.01f;
    float dt = 0.45f * W.cell_w / sqrtf(WF_GRAVITY * maxd);
    if (dt > 0.033f) dt = 0.033f;    /* never coarser than ~1 frame  */
    if (dt < 0.002f) dt = 0.002f;    /* nor finer than is affordable */
    return dt;
}

/* Solid things standing in the water. Replaces the WHOLE set every call rather
   than adding to it: the caller rescans the scene periodically, and an additive
   API would pile duplicate rocks up forever -- the same bug the springs had.

   Boxes are { min_x, min_z, max_x, max_z }. The revision only moves when the
   mask actually CHANGED, so a rock that has not moved does not force everything
   keyed on the field (waterfall geometry, GPU uploads) to rebuild 30 times a
   second. */
int g3d_waterfield_set_obstacles(const float *boxes, int n) {
    if (!W.active) return 0;
    int S = W.side, N = S * S;
    float cell = W.world_size / (float)(S - 1);
    float ox = -W.world_size * 0.5f, oz = -W.world_size * 0.5f;

    if (n <= 0 || !boxes) {
        if (!W.obst) return 0;
        free(W.obst); W.obst = NULL;
        W.revision = ++g_wf_revision_seq;
        return 0;
    }

    unsigned char *mask = (unsigned char *)calloc((size_t)N, 1);
    if (!mask) return 0;

    int blocked = 0;
    for (int b = 0; b < n; b++) {
        const float *r = &boxes[b * 4];
        int i0 = (int)floorf((r[0] - ox) / cell);
        int i1 = (int)ceilf ((r[2] - ox) / cell);
        int j0 = (int)floorf((r[1] - oz) / cell);
        int j1 = (int)ceilf ((r[3] - oz) / cell);
        if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
        if (i1 > S - 1) i1 = S - 1; if (j1 > S - 1) j1 = S - 1;
        for (int j = j0; j <= j1; j++)
            for (int i = i0; i <= i1; i++)
                if (!mask[j * S + i]) { mask[j * S + i] = 1; blocked++; }
    }

    if (W.obst && memcmp(W.obst, mask, (size_t)N) == 0) { free(mask); return blocked; }

    /* Water caught under a rock that just appeared is DISPLACED, not deleted:
       it goes to whichever neighbours are still free. Dropping it instead would
       make a river lose volume every time something was placed in it. */
    if (W.d) {
        for (int j = 0; j < S; j++)
            for (int i = 0; i < S; i++) {
                int c = j * S + i;
                if (!mask[c] || W.d[c] <= 0.0f) continue;
                int nb[4], nn = 0;
                if (i > 0     && !mask[c - 1]) nb[nn++] = c - 1;
                if (i < S - 1 && !mask[c + 1]) nb[nn++] = c + 1;
                if (j > 0     && !mask[c - S]) nb[nn++] = c - S;
                if (j < S - 1 && !mask[c + S]) nb[nn++] = c + S;
                if (!nn) continue;
                float share = W.d[c] / (float)nn;
                for (int k = 0; k < nn; k++) W.d[nb[k]] += share;
                W.d[c] = 0.0f;
            }
    }

    free(W.obst);
    W.obst = mask;
    W.revision = ++g_wf_revision_seq;
    return blocked;
}

int g3d_waterfield_obstacle_at(float x, float z) {
    if (!W.active || !W.obst) return 0;
    int S = W.side;
    float cell = W.world_size / (float)(S - 1);
    int i = (int)((x + W.world_size * 0.5f) / cell + 0.5f);
    int j = (int)((z + W.world_size * 0.5f) / cell + 0.5f);
    if (i < 0 || j < 0 || i >= S || j >= S) return 0;
    return W.obst[j * S + i] ? 1 : 0;
}

void g3d_waterfield_step(float dt) {
    if (!W.active || dt <= 0.0f) return;
    if (dt > 0.25f) dt = 0.25f;          /* ignore hitches / breakpoints */

    const int MAX_SUB = 8;
    float maxd = 0.0f;
    for (int j = W.bj0; j <= W.bj1; j++)
        for (int i = W.bi0; i <= W.bi1; i++) {
            float dep = W.d[j * W.side + i];
            if (dep > maxd) maxd = dep;
        }

    W.accum += dt;
    int n = 0;
    while (n < MAX_SUB) {
        float h = wf_stable_step(maxd);
        if (W.accum < h) break;
        maxd = wf_substep(h);
        W.accum -= h;
        n++;
    }
    if (n == MAX_SUB) W.accum = 0.0f;    /* fell behind: drop the backlog */
    if (n > 0) W.revision = ++g_wf_revision_seq;
}

void g3d_waterfield_settle(float seconds) {
    if (!W.active || seconds <= 0.0f) return;

    /* El paso estable se calcula con el agua MAS HONDA que ya hay. Arrancar de
       cero daba por bueno el paso mas grande posible en la primera iteracion,
       que sobre un campo ya lleno viola el CFL de largo: la primera pasada sale
       inestable y el paso se desploma para el resto, de modo que un manantial
       recien puesto inyecta pero su agua no llega a moverse -- se queda una
       columna clavada en su celda.
       Solo se notaba desde un juego, porque ahi el mar se asienta ANTES de
       crear los manantiales; en el editor el orden es el contrario. */
    float t = 0.0f, maxd = 0.0f;
    for (int i = 0, n = W.side * W.side; i < n; i++)
        if (W.d[i] > maxd) maxd = W.d[i];
    int guard = 0;
    /* El tope tiene que dar para los segundos PEDIDOS. Estaba fijo en 8000
       sub-pasos: con el paso estable de un campo lleno eso son unos 60 s, asi
       que pedir 600 daba lo mismo que pedir 60 -- el rio nunca acababa de
       llenarse por mucho que subieras el mando. */
    int limite = (int)(seconds / 0.002f) + 1000;   /* 0.002 = paso minimo */
    if (limite < 8000) limite = 8000;
    while (t < seconds && guard < limite) {
        float h = wf_stable_step(maxd);
        maxd = wf_substep(h);
        t += h;
        guard++;
    }
    W.accum = 0.0f;
    W.revision = ++g_wf_revision_seq;
}

/* --------------------------------------------------------------------------
   Queries
   -------------------------------------------------------------------------- */

float g3d_waterfield_terrain_at(float x, float z) {
    if (!W.active) return 0.0f;
    int c[4]; float w[4];
    if (!wf_bilinear_setup(x, z, c, w)) return 0.0f;
    return W.terr[c[0]] * w[0] + W.terr[c[1]] * w[1]
         + W.terr[c[2]] * w[2] + W.terr[c[3]] * w[3];
}

float g3d_waterfield_level_at(float x, float z) {
    if (!W.active) return G3D_NO_WATER;
    int c[4]; float w[4];
    if (!wf_bilinear_setup(x, z, c, w)) {
        /* Outside the field the sea, if any, still extends to the horizon. */
        return (W.sea > G3D_NO_WATER_TEST) ? W.sea : G3D_NO_WATER;
    }
    /* Interpolate over the WET corners only, renormalising their weights. A dry
       corner sits on high ground; folding its terrain height into the average
       would tilt the surface up into the bank and make objects near the shore
       float above the water. */
    float sum = 0.0f, wsum = 0.0f;
    for (int k = 0; k < 4; k++) {
        if (W.d[c[k]] > WF_WET_EPS) {
            sum  += (W.terr[c[k]] + W.d[c[k]]) * w[k];
            wsum += w[k];
        }
    }
    if (wsum < 1e-5f) return G3D_NO_WATER;
    return sum / wsum;
}

float g3d_waterfield_depth_at(float x, float z) {
    if (!W.active) return 0.0f;
    float lvl = g3d_waterfield_level_at(x, z);
    if (lvl < G3D_NO_WATER_TEST) return 0.0f;
    float t = g3d_waterfield_terrain_at(x, z);
    return (lvl > t) ? (lvl - t) : 0.0f;
}

void g3d_waterfield_flow_at(float x, float z, float *vx, float *vz) {
    if (vx) *vx = 0.0f;
    if (vz) *vz = 0.0f;
    if (!W.active) return;
    int c[4]; float w[4];
    if (!wf_bilinear_setup(x, z, c, w)) return;
    float ax = 0.0f, az = 0.0f, wsum = 0.0f;
    for (int k = 0; k < 4; k++) {
        if (W.d[c[k]] > WF_WET_EPS) {
            ax   += W.vx[c[k]] * w[k];
            az   += W.vz[c[k]] * w[k];
            wsum += w[k];
        }
    }
    if (wsum < 1e-5f) return;
    if (vx) *vx = ax / wsum;
    if (vz) *vz = az / wsum;
}

int g3d_waterfield_wet_bounds(int *i0, int *j0, int *i1, int *j1) {
    if (!W.active || !W.any_wet) return 0;
    if (i0) *i0 = W.bi0;
    if (j0) *j0 = W.bj0;
    if (i1) *i1 = W.bi1;
    if (j1) *j1 = W.bj1;
    return 1;
}
