/*
 * libmod_3d_water_falls.c - Falling water from the unified field
 *
 * Scans the field for ledges that water is actually running over and hangs a
 * vertical sheet at each one. The scan runs only when the field's revision
 * changes, and only over its wet bounding box, so a still lake costs nothing and
 * a river costs a handful of quads.
 */

#include "libmod_3d_water_falls.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_shaders.h"
#include "libmod_3d_glcaps.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef VITA
#ifdef _WIN32
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <SDL2/SDL.h>

#define WF_MAX_SHEETS 4096
#define WF_FLOATS_PER_VERT 6      /* xyz + (u, v, fallHeight) */
#define WF_MAX_FEET 256
#define WF_MAX_OBST 64
/* En cuantas tiras se parte una cortina que se topa con una roca. Solo se usa
   cuando de verdad hay algo delante: sin rocas la cortina sigue siendo UN quad,
   como antes, asi que esto no cuesta nada en el caso normal. */
#define WF_SPLIT_COLS 8

static struct {
    int inited, failed;
    G3DShaderProgram *prog;
    unsigned int vao, vbo;
    int sheets;                   /* quads currently built */
    int capacity;                 /* verts the VBO can hold */
    unsigned int field_rev;       /* field revision the geometry reflects */
    float threshold;
    float foam, mist;

    /* Where each fall LANDS: x, y, z, height, width. The spray at the foot of a
       waterfall has to come from the same place the curtain does, or it drifts
       off it as soon as the river shifts. */
    float feet[WF_MAX_FEET * 5];
    int   nfeet;

    /* Rocks standing in the falling water. Kept here and not in the field
       because the field's mask is flat and only covers what is standing in
       water deep enough to register -- neither is true of a rock halfway down
       a cliff. */
    float obst[WF_MAX_OBST * 6];
    int   nobst;
    unsigned int obst_rev;        /* bumped when the set really changes */
    float thick;                  /* grosor del chorro: una celda del campo */
    unsigned int built_obst_rev;  /* the set the geometry reflects */
    int   splits;                 /* curtains parted last build */
} F = { .threshold = 1.5f, .foam = 1.0f, .mist = 1.0f };

static char *falls_src(const char *body, int frag) {
    const G3DGLCaps *c = g3d_glcaps();
    const char *ver = c->es ? "#version 300 es\n" : "#version 330 core\n";
    const char *prec = (c->es && frag) ? "precision highp float;\n" : "";
    size_t n = strlen(ver) + strlen(prec) + strlen(body) + 1;
    char *s = (char *)malloc(n);
    if (!s) return NULL;
    strcpy(s, ver); strcat(s, prec); strcat(s, body);
    return s;
}

/* Emit one vertical quad spanning the shared edge between two cells, from the
   upper water surface down to whatever is below (the lower water, or the ground
   if it is dry). */
/* `v0`/`v1` are this segment's position within the WHOLE fall (0 at the lip, 1
   where it lands) and `total` is the fall's full height. Passing those rather
   than the segment's own extent is what makes a cascade read as one curtain:
   the foam keeps building and the streaks keep accelerating all the way down,
   instead of resetting at every step and looking like a staircase. */
static void falls_push_quad(float *v, int *n, float ax, float az, float bx, float bz,
                            float top, float bottom, float v0, float v1, float total) {
    const int   ai[6] = { 1, 0, 1, 0, 1, 0 };   /* 1 = use a, 0 = use b */
    const int   ti[6] = { 1, 1, 0, 1, 0, 0 };   /* 1 = top, 0 = bottom  */
    /* The across-the-fall coordinate must be a WORLD position, not 0..1 per
       quad. A wide cliff is made of many one-cell-wide sheets side by side; with
       a per-quad 0..1 every one of them repeats exactly the same streak pattern,
       and the fall reads as a row of identical rectangles. The edge is always
       axis-aligned, so whichever of x/z varies along it is the coordinate. */
    int along_x = (ax != bx);
    for (int k = 0; k < 6; k++) {
        float x = ai[k] ? ax : bx;
        float z = ai[k] ? az : bz;
        float y = ti[k] ? top : bottom;
        float *p = &v[(*n) * WF_FLOATS_PER_VERT];
        p[0] = x; p[1] = y; p[2] = z;
        p[3] = along_x ? x : z;
        p[4] = ti[k] ? v0 : v1;
        p[5] = total;
        (*n)++;
    }
}

/* El trozo de roca que tapa la columna (x,z), si es que hay alguna. Devuelve 1 y
   rellena el tramo vertical que estorba. Con varias rocas encima se queda con la
   union, que para una cortina de un metro de ancho es lo mismo y evita partirla
   en pedazos que luego se solapan. */
static int falls_blocked_at(float x, float z, float *oy0, float *oy1) {
    int hit = 0;
    float th = F.thick;
    for (int b = 0; b < F.nobst; b++) {
        const float *r = &F.obst[b * 6];
        if (x < r[0] - th || x > r[3] + th || z < r[2] - th || z > r[5] + th) continue;
        if (!hit) { *oy0 = r[1]; *oy1 = r[4]; hit = 1; }
        else { if (r[1] < *oy0) *oy0 = r[1]; if (r[4] > *oy1) *oy1 = r[4]; }
    }
    return hit;
}

/* ¿Estorba algo a esta cortina? Se mira antes de partir nada: si no hay roca
   delante -- que es lo normal -- la cortina sale de una pieza igual que siempre,
   sin columnas ni costuras. */
static int falls_any_obstacle(float ax, float az, float bx, float bz,
                              float top, float bottom) {
    if (F.nobst <= 0) return 0;
    /* La cortina se guarda como un plano de GROSOR CERO: para un salto orientado
       en x, todos sus vertices comparten la misma x. Pedir que la caja de la roca
       cruce ese plano exacto no lo cumple casi nunca -- basta con que la piedra
       este medio metro delante o detras y el agua le pasaba por dentro igual que
       antes. Un rio cayendo no es una lamina infinitamente fina, asi que se le da
       el grosor de una celda, que es el ancho real del chorro. */
    float th = F.thick;
    float x0 = (ax < bx ? ax : bx) - th, x1 = (ax < bx ? bx : ax) + th;
    float z0 = (az < bz ? az : bz) - th, z1 = (az < bz ? bz : az) + th;
    for (int b = 0; b < F.nobst; b++) {
        const float *r = &F.obst[b * 6];
        if (r[3] < x0 || r[0] > x1) continue;
        if (r[5] < z0 || r[2] > z1) continue;
        if (r[4] < bottom || r[1] > top) continue;   /* pasa por encima o por debajo */
        return 1;
    }
    return 0;
}

/* Suelta una cortina, partiendola si hay una roca delante.
 *
 * El agua que cae contra una piedra no la atraviesa: se abre a los lados, deja
 * seco lo que hay justo detras y vuelve a juntarse mas abajo. Eso es lo que hace
 * esto -- la cortina se divide en tiras a lo ancho, y la tira que se topa con la
 * roca se corta en dos trozos, el de encima y el de debajo, dejando el hueco.
 *
 * Sin rocas delante sale UN quad, exactamente el de antes: ni una costura de mas
 * ni un triangulo de mas en el caso normal, que es la inmensa mayoria. */
static void falls_emit_sheet(float *v, int *nv, int *quads,
                             float ax, float az, float bx, float bz,
                             float top, float bottom, float v0, float v1,
                             float total) {
    if (*quads >= WF_MAX_SHEETS) return;
    if (top - bottom <= 0.001f) return;

    if (!falls_any_obstacle(ax, az, bx, bz, top, bottom)) {
        falls_push_quad(v, nv, ax, az, bx, bz, top, bottom, v0, v1, total);
        (*quads)++;
        return;
    }

    float span = top - bottom;
    int parted = 0;
    for (int cix = 0; cix < WF_SPLIT_COLS && *quads < WF_MAX_SHEETS - 2; cix++) {
        float f0 = (float)cix / (float)WF_SPLIT_COLS;
        float f1 = (float)(cix + 1) / (float)WF_SPLIT_COLS;
        float cx0 = ax + (bx - ax) * f0, cz0 = az + (bz - az) * f0;
        float cx1 = ax + (bx - ax) * f1, cz1 = az + (bz - az) * f1;
        float mx = (cx0 + cx1) * 0.5f, mz = (cz0 + cz1) * 0.5f;

        float oy0, oy1;
        if (!falls_blocked_at(mx, mz, &oy0, &oy1) || oy1 <= bottom || oy0 >= top) {
            falls_push_quad(v, nv, cx0, cz0, cx1, cz1, top, bottom, v0, v1, total);
            (*quads)++;
            continue;
        }
        if (oy0 < bottom) oy0 = bottom;
        if (oy1 > top)    oy1 = top;
        parted = 1;

        /* El trozo de ARRIBA, hasta donde empieza la roca. */
        if (top - oy1 > 0.02f) {
            float vb = v0 + (v1 - v0) * (top - oy1) / span;
            falls_push_quad(v, nv, cx0, cz0, cx1, cz1, top, oy1, v0, vb, total);
            (*quads)++;
        }
        /* Y el de ABAJO, desde el pie de la roca: es lo que hace que el agua se
           vuelva a juntar en vez de quedar cortada hasta el suelo. */
        if (oy0 - bottom > 0.02f) {
            float va = v0 + (v1 - v0) * (top - oy0) / span;
            falls_push_quad(v, nv, cx0, cz0, cx1, cz1, oy0, bottom, va, v1, total);
            (*quads)++;
        }
        /* Y donde golpea, que salpique. Se apunta como un pie de cascada mas: la
           espuma sale de la misma lista que la del fondo, asi que no puede
           quedarse desplazada respecto a la cortina. */
        if (F.nfeet < WF_MAX_FEET && oy1 < top - 0.02f) {
            float *ft = &F.feet[F.nfeet * 5];
            ft[0] = mx; ft[1] = oy1; ft[2] = mz;
            ft[3] = top - oy1;                       /* lo que ha caido hasta pegar */
            ft[4] = fabsf(cx1 - cx0) + fabsf(cz1 - cz0);
            F.nfeet++;
        }
    }
    if (parted) F.splits++;
}

/* The shared edge between a cell and the neighbour it spills into. */
static void falls_edge(int i, int j, int di, int dj, int S, float ws, float cell,
                       float *ax, float *az, float *bx, float *bz) {
    float x0 = ((float)i / (float)(S - 1) - 0.5f) * ws;
    float z0 = ((float)j / (float)(S - 1) - 0.5f) * ws;
    if (di != 0) {
        float ex = x0 + (di > 0 ? cell * 0.5f : -cell * 0.5f);
        *ax = ex; *az = z0 - cell * 0.5f;
        *bx = ex; *bz = z0 + cell * 0.5f;
    } else {
        float ez = z0 + (dj > 0 ? cell * 0.5f : -cell * 0.5f);
        *ax = x0 - cell * 0.5f; *az = ez;
        *bx = x0 + cell * 0.5f; *bz = ez;
    }
}

/* Rebuild the sheets from the current field. */
static void falls_build(void);

/* Rebuild only when the field really changed. Both the curtains and the spray
   at their feet come off the same build, so neither can drift from the other. */
static void falls_build_if_needed(void) {
    unsigned int rev = g3d_waterfield_revision();
    /* Tambien hay que rehacerla si se han movido las rocas: la cortina se parte
       segun donde estan, asi que un cambio ahi cambia la geometria igual que un
       cambio del campo. */
    if (rev != F.field_rev || F.obst_rev != F.built_obst_rev) {
        F.field_rev = rev; F.built_obst_rev = F.obst_rev; falls_build();
    }
}

static void falls_build(void) {
    F.sheets = 0;
    F.nfeet = 0;
    F.splits = 0;
    if (!g3d_waterfield_active()) return;

    int S = g3d_waterfield_side();
    float ws = g3d_waterfield_world_size();
    const float *d = g3d_waterfield_depth_array();
    const float *t = g3d_waterfield_terrain_array();
    int i0, j0, i1, j1;
    if (!g3d_waterfield_wet_bounds(&i0, &j0, &i1, &j1)) return;

    float cell = ws / (float)(S - 1);
    F.thick = cell * 0.75f;
    float *verts = (float *)malloc((size_t)WF_MAX_SHEETS * 6 * WF_FLOATS_PER_VERT * sizeof(float));
    if (!verts) return;
    int nv = 0, quads = 0;

    /* Cells already claimed by a fall further up: a cascade is ONE fall, so the
       cells partway down must not each start their own. */
    char *used = (char *)calloc((size_t)S * S, 1);
    if (!used) { free(verts); return; }

    const int di[4] = { -1, 1, 0, 0 };
    const int dj[4] = { 0, 0, -1, 1 };
    /* What makes a waterfall is STEEPNESS, not total descent. Comparing the drop
       against a fixed height instead of against the cell size means the same
       terrain sprouts curtains or not purely according to the grid resolution:
       on the editor's 2.5-unit cells a 0.3 threshold is a 7 degree slope, so an
       ordinary hillside would be draped in falling water. Requiring the drop to
       exceed roughly one cell width asks for about 45 degrees before a step
       counts as falling at all. */
    const float STEP_MIN = cell;
    #define WF_MAX_CHAIN 96

    for (int j = j0; j <= j1 && quads < WF_MAX_SHEETS; j++) {
        for (int i = i0; i <= i1 && quads < WF_MAX_SHEETS; i++) {
            int c = j * S + i;
            if (used[c] || d[c] <= 0.01f) continue;      /* no water here to fall */

            /* Follow the water down the steepest descent until it lands: either
               the ground levels off or it hits a pool. A tall cascade is a chain
               of cell-sized steps, and treating each step separately is what
               made it look like a staircase instead of a curtain. */
            int ci = i, cj = j;
            float top = t[c] + d[c];
            int chainCell[WF_MAX_CHAIN], chainDir[WF_MAX_CHAIN];
            float chainTop[WF_MAX_CHAIN], chainBot[WF_MAX_CHAIN];
            int nchain = 0;

            while (nchain < WF_MAX_CHAIN) {
                int cc = cj * S + ci;
                float surf = (d[cc] > 0.01f) ? (t[cc] + d[cc]) : t[cc];
                int best = -1;
                float bestDrop = STEP_MIN;
                for (int k = 0; k < 4; k++) {
                    int ni = ci + di[k], nj = cj + dj[k];
                    if (ni < 0 || nj < 0 || ni >= S || nj >= S) continue;
                    int nc = nj * S + ni;
                    float below = (d[nc] > 0.01f) ? (t[nc] + d[nc]) : t[nc];
                    float drop = surf - below;
                    if (drop > bestDrop) { bestDrop = drop; best = k; }
                }
                if (best < 0) break;                     /* it has levelled out */

                int nc = (cj + dj[best]) * S + (ci + di[best]);
                float below = (d[nc] > 0.01f) ? (t[nc] + d[nc]) : t[nc];
                chainCell[nchain] = cc;
                chainDir[nchain]  = best;
                chainTop[nchain]  = surf;
                chainBot[nchain]  = below;
                nchain++;
                used[cc] = 1;

                if (d[nc] > 0.5f) break;                 /* it landed in a pool */
                ci += di[best]; cj += dj[best];
            }

            if (nchain == 0) continue;
            float total = top - chainBot[nchain - 1];
            if (total < F.threshold) continue;           /* a slope, not a fall */

            /* Record the landing point before splitting into branches: both
               paths end at the same place, and the spray only cares about that. */
            if (F.nfeet < WF_MAX_FEET) {
                int lc = chainCell[nchain - 1], lk = chainDir[nchain - 1];
                float ex0, ez0, ex1, ez1;
                falls_edge(lc % S, lc / S, di[lk], dj[lk], S, ws, cell,
                           &ex0, &ez0, &ex1, &ez1);
                float *ft = &F.feet[F.nfeet * 5];
                ft[0] = (ex0 + ex1) * 0.5f;
                ft[1] = chainBot[nchain - 1];
                ft[2] = (ez0 + ez1) * 0.5f;
                ft[3] = total;
                ft[4] = cell;
                F.nfeet++;
            }

            /* A near-vertical drop becomes ONE sheet hanging at the lip, all the
               way down to where the water lands.
             *
             * Water leaving a ledge falls straight; it does not hug the rock. And
             * following the cells there means several semi-transparent panels,
             * each offset a cell sideways from the last, overlapping into visible
             * horizontal seams across the curtain. One quad has no seams and is
             * closer to what actually happens. Longer, shallower chutes keep the
             * per-segment sheets, because there a vertical curtain would visibly
             * detach from the slope the water is really running down. */
            if (nchain <= 8) {
                float landing = chainBot[nchain - 1];
                int cc = chainCell[0];
                int kk = chainDir[0];
                float ax, az, bx, bz;
                falls_edge(cc % S, cc / S, di[kk], dj[kk], S, ws, cell,
                           &ax, &az, &bx, &bz);
                falls_emit_sheet(verts, &nv, &quads, ax, az, bx, bz, top, landing,
                                 0.0f, 1.0f, total);
                continue;
            }

            /* Emit the chain as one curtain: each segment carries its position
               within the whole fall, so foam and speed carry across the steps. */
            float fallen = 0.0f;
            for (int k = 0; k < nchain && quads < WF_MAX_SHEETS; k++) {
                float segTop = chainTop[k], segBot = chainBot[k];
                float seg = segTop - segBot;
                if (seg <= 0.0f) continue;
                float v0 = fallen / total;
                float v1 = (fallen + seg) / total;
                fallen += seg;

                int cc = chainCell[k];
                int kk = chainDir[k];
                float ax, az, bx, bz;
                falls_edge(cc % S, cc / S, di[kk], dj[kk], S, ws, cell,
                           &ax, &az, &bx, &bz);
                falls_emit_sheet(verts, &nv, &quads, ax, az, bx, bz, segTop, segBot,
                                 v0, v1 > 1.0f ? 1.0f : v1, total);
            }
        }
    }
    free(used);
    #undef WF_MAX_CHAIN

    if (nv > 0) {
        if (!F.vao) glGenVertexArrays(1, &F.vao);
        if (!F.vbo) glGenBuffers(1, &F.vbo);
        glBindVertexArray(F.vao);
        glBindBuffer(GL_ARRAY_BUFFER, F.vbo);
        if (nv > F.capacity) {
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)nv * WF_FLOATS_PER_VERT * sizeof(float),
                         verts, GL_DYNAMIC_DRAW);
            F.capacity = nv;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)nv * WF_FLOATS_PER_VERT * sizeof(float), verts);
        }
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              WF_FLOATS_PER_VERT * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              WF_FLOATS_PER_VERT * sizeof(float),
                              (void *)(3 * sizeof(float)));
        glBindVertexArray(0);
    }
    F.sheets = quads;
    free(verts);
}

void g3d_water_falls_render(G3DCamera *camera, int flip_y) {
    if (!camera || F.failed || !g3d_waterfield_active()) return;

    if (!F.inited) {
        char *v = falls_src(g3d_water_glsl_fall_vert, 0);
        char *f = falls_src(g3d_water_glsl_fall_frag, 1);
        if (v && f) F.prog = g3d_shader_create(v, f);
        free(v); free(f);
        if (!F.prog) { F.failed = 1; return; }
        F.inited = 1;
        F.field_rev = 0;
    }

    falls_build_if_needed();
    if (F.sheets <= 0) return;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }

    G3DShaderProgram *sh = F.prog;
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uView", view);
    g3d_shader_set_mat4(sh, "uProjection", proj);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);
    g3d_shader_set_float(sh, "uTime", (float)SDL_GetTicks() / 1000.0f);
    g3d_shader_set_float(sh, "uFoamAmount", F.foam);
    g3d_shader_set_float(sh, "uMist", F.mist);
    g3d_shader_set_vec3(sh, "uScatterColor", vec3_make(0.30f, 0.45f, 0.50f));

    {   /* same fallback as the surface: unlit water renders black */
        float sd[3] = { 0 }, sc[3] = { 0 }, amb[3] = { 0 };
        g3d_sky_get_sun(sd, sc);
        g3d_sky_get_ambient(amb);
        if (sc[0] + sc[1] + sc[2] < 1e-4f) { sc[0] = 1.0f; sc[1] = 0.97f; sc[2] = 0.92f; }
        if (amb[0] + amb[1] + amb[2] < 1e-4f) { amb[0] = 0.22f; amb[1] = 0.27f; amb[2] = 0.33f; }
        g3d_shader_set_vec3(sh, "uSunColor", vec3_make(sc[0], sc[1], sc[2]));
        g3d_shader_set_vec3(sh, "uAmbient", vec3_make(amb[0], amb[1], amb[2]));
    }
    {
        int fen = 0; Vec3 fcol = vec3_make(0.7f, 0.78f, 0.88f); float fst = 0.0f, fnd = 1.0f;
        g3d_renderer_get_fog(&fen, &fcol, &fst, &fnd);
        g3d_shader_set_int(sh, "uFogEnabled", fen);
        g3d_shader_set_vec3(sh, "uFogColor", fcol);
        g3d_shader_set_float(sh, "uFogStart", fst);
        g3d_shader_set_float(sh, "uFogEnd", fnd);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);      /* a curtain is visible from both sides */

    glBindVertexArray(F.vao);
    glDrawArrays(GL_TRIANGLES, 0, F.sheets * 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
}

void g3d_water_falls_set_threshold(float drop) {
    F.threshold = drop > 0.05f ? drop : 0.05f;
    F.field_rev = 0;              /* force a rebuild */
}

void g3d_water_falls_set_style(float foam, float mist) {
    F.foam = foam < 0.0f ? 0.0f : foam;
    F.mist = mist < 0.0f ? 0.0f : mist;
}

int g3d_water_falls_feet(float *out, int max) {
    falls_build_if_needed();
    int n = F.nfeet < max ? F.nfeet : max;
    if (out) memcpy(out, F.feet, (size_t)n * 5 * sizeof(float));
    return n;
}

int g3d_water_falls_count(void) { return F.sheets; }
int g3d_water_falls_split_count(void) { return F.splits; }

void g3d_water_falls_set_obstacles(const float *boxes, int n) {
    if (n < 0) n = 0;
    if (n > WF_MAX_OBST) n = WF_MAX_OBST;
    if (!boxes) n = 0;
    /* Solo se toca la revision si la lista CAMBIA de verdad. Esto se llama cada
       frame desde el recorrido de la escena, y sin la comparacion una roca
       quieta reconstruiria toda la geometria sesenta veces por segundo. */
    if (n == F.nobst &&
        (n == 0 || memcmp(F.obst, boxes, (size_t)n * 6 * sizeof(float)) == 0)) return;
    if (n > 0) memcpy(F.obst, boxes, (size_t)n * 6 * sizeof(float));
    F.nobst = n;
    F.obst_rev++;
}

void g3d_water_falls_shutdown(void) {
    if (F.prog) { g3d_shader_free(F.prog); F.prog = NULL; }
    if (F.vbo) { glDeleteBuffers(1, &F.vbo); F.vbo = 0; }
    if (F.vao) { glDeleteVertexArrays(1, &F.vao); F.vao = 0; }
    F.inited = 0; F.failed = 0; F.sheets = 0; F.capacity = 0; F.field_rev = 0;
}

#else /* VITA */

void g3d_water_falls_render(G3DCamera *c, int f) { (void)c; (void)f; }
void g3d_water_falls_set_threshold(float d) { (void)d; }
void g3d_water_falls_set_style(float f, float m) { (void)f; (void)m; }
int  g3d_water_falls_count(void) { return 0; }
int  g3d_water_falls_split_count(void) { return 0; }
void g3d_water_falls_set_obstacles(const float *b, int n) { (void)b; (void)n; }
void g3d_water_falls_shutdown(void) {}

#endif
