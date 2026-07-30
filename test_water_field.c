/* Standalone behaviour check for the unified water field. */
#include "libmod_3d_water_field.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define S 65
#define WS 64.0f

static float terr[S * S];
static int fails = 0;

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) fails++;
}

/* Terrain: slopes down along +x from y=20 to y=0, with a bowl carved in the
   middle. Water from a spring at the high end should run downhill, pool in the
   bowl until it spills, then continue to the low edge. */
static void build_terrain(void) {
    for (int j = 0; j < S; j++) {
        for (int i = 0; i < S; i++) {
            float u = (float)i / (float)(S - 1);
            float h = 20.0f * (1.0f - u);
            float bx = (float)i - 32.0f, bz = (float)j - 32.0f;
            float r = sqrtf(bx * bx + bz * bz);
            if (r < 8.0f) h -= 6.0f * (1.0f - r / 8.0f);   /* the bowl */
            terr[j * S + i] = h;
        }
    }
}

static float wx(int i) { return ((float)i / (float)(S - 1) - 0.5f) * WS; }

int main(void) {
    build_terrain();

    printf("1. init\n");
    check("field initialises", g3d_waterfield_init(terr, S, WS), "");
    check("side matches", g3d_waterfield_side() == S, "");
    check("dry field reports no water",
          g3d_waterfield_level_at(0.0f, 0.0f) < G3D_NO_WATER_TEST, "");

    printf("2. a spring fills the bowl and runs downhill\n");
    g3d_waterfield_set_evaporation(0.0f);   /* isolate transport from losses */
    int sp = g3d_waterfield_add_spring(wx(4), wx(32), 3.0f);
    check("spring added", sp >= 0, "");
    g3d_waterfield_settle(90.0f);

    float bowl = g3d_waterfield_level_at(0.0f, 0.0f);
    char buf[160];
    snprintf(buf, sizeof(buf), "level=%.3f terrain=%.3f", bowl,
             g3d_waterfield_terrain_at(0.0f, 0.0f));
    check("bowl holds water", bowl > G3D_NO_WATER_TEST, buf);

    float depth_bowl = g3d_waterfield_depth_at(0.0f, 0.0f);
    snprintf(buf, sizeof(buf), "depth=%.3f", depth_bowl);
    check("bowl water is deep (pooled, not a film)", depth_bowl > 1.0f, buf);

    /* A pool's surface must be level. Only POOLED cells count: the thin film
       running down the slope into the bowl is water too, but its level tracks
       the terrain, so including it would measure the hillside, not the lake. */
    float lo = 1e30f, hi = -1e30f, deepest = -1e30f, at_deepest = 0.0f;
    for (int i = 20; i <= 46; i++) {
        float d = g3d_waterfield_depth_at(wx(i), 0.0f);
        if (d <= 0.5f) continue;
        float l = g3d_waterfield_level_at(wx(i), 0.0f);
        if (l < lo) lo = l;
        if (l > hi) hi = l;
        if (d > deepest) { deepest = d; at_deepest = l; }
    }
    snprintf(buf, sizeof(buf), "spread=%.4f over the pooled cells", hi - lo);
    check("pooled surface is flat", (hi - lo) < 0.35f, buf);

    /* The deepest cell must not sit BELOW its neighbours: a lake cannot have a
       dent in its own surface. This is what catches the routing surface drifting
       away from the real ground. */
    snprintf(buf, sizeof(buf), "deepest cell level=%.3f, pool min=%.3f",
             at_deepest, lo);
    check("no dent at the deepest point", at_deepest <= lo + 0.05f, buf);

    /* Water must reach downstream of the bowl, and must NOT climb uphill
       past the spring. */
    float downstream = g3d_waterfield_depth_at(wx(55), 0.0f);
    snprintf(buf, sizeof(buf), "depth=%.4f at x=%.1f", downstream, wx(55));
    check("water flows downstream of the bowl", downstream > 0.0f, buf);

    float uphill = g3d_waterfield_depth_at(wx(1), 0.0f);
    snprintf(buf, sizeof(buf), "depth=%.4f at the high edge", uphill);
    check("water does not climb uphill past the spring", uphill < 0.5f, buf);

    printf("3. water never sits above the terrain\n");
    const float *d = g3d_waterfield_depth_array();
    int negative = 0;
    for (int c = 0; c < S * S; c++) if (d[c] < 0.0f) negative++;
    check("no negative depths", negative == 0, "");

    printf("4. flow points downhill, at a believable speed\n");
    float vx = 0.0f, vz = 0.0f;
    g3d_waterfield_flow_at(wx(50), 0.0f, &vx, &vz);
    snprintf(buf, sizeof(buf), "v=(%.3f, %.3f)", vx, vz);
    check("flow runs in +x (downhill)", vx > 0.0f, buf);

    /* The pipes model has no friction, so an unguarded thin film reports tens of
       units per second -- about 100 km/h for an ordinary stream. Real channel
       flow is Froude-limited near sqrt(g*h). This drives the ripple scroll and
       the drift on floating objects, so an absurd value is very visible. */
    float speed = sqrtf(vx * vx + vz * vz);
    float depth_here = g3d_waterfield_depth_at(wx(50), 0.0f);
    float froude_cap = 2.0f * sqrtf(9.81f * (depth_here > 0.05f ? depth_here : 0.05f));
    snprintf(buf, sizeof(buf), "%.2f u/s, cap %.2f", speed, froude_cap);
    check("stream speed stays physical", speed <= froude_cap + 0.01f, buf);

    printf("5. sea level floods everything below it\n");
    g3d_waterfield_clear_springs();
    g3d_waterfield_set_sea_level(5.0f);
    g3d_waterfield_step(0.05f);
    float sea_here = g3d_waterfield_level_at(wx(60), wx(10));   /* terrain ~1.2 */
    snprintf(buf, sizeof(buf), "level=%.3f (want ~5)", sea_here);
    check("low ground sits at sea level", fabsf(sea_here - 5.0f) < 0.75f, buf);

    float high = g3d_waterfield_terrain_at(wx(2), wx(2));
    float dry = g3d_waterfield_depth_at(wx(2), wx(2));
    snprintf(buf, sizeof(buf), "terrain=%.2f depth=%.3f", high, dry);
    check("ground above sea level stays dry", dry < 0.5f, buf);

    printf("6. wet bounds track the water\n");
    int i0, j0, i1, j1;
    check("bounds reported while wet",
          g3d_waterfield_wet_bounds(&i0, &j0, &i1, &j1) == 1, "");

    printf("7. queries outside the field\n");
    float outside = g3d_waterfield_level_at(WS * 5.0f, 0.0f);
    snprintf(buf, sizeof(buf), "level=%.2f", outside);
    check("the sea extends past the field edge", fabsf(outside - 5.0f) < 0.01f, buf);

    g3d_waterfield_set_sea_level(G3D_NO_WATER);
    check("no sea -> nothing outside the field",
          g3d_waterfield_level_at(WS * 5.0f, 0.0f) < G3D_NO_WATER_TEST, "");

    g3d_waterfield_shutdown();
    check("shutdown deactivates", g3d_waterfield_active() == 0, "");

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
