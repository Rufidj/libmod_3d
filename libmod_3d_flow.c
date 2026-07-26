/*
 * libmod_3d_flow.c - Flowing water (waterfalls / rivers) implementation
 */

#include "libmod_3d_flow.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_chunkterrain.h"   /* g3d_heightfield_height */
#include "libmod_3d_scene.h"
#include "libmod_3d_light.h"
#include "libmod_3d_water.h"   /* g3d_water_add_ripple_source (salpicadura al pie) */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define G3D_MAX_FLOWS 16
#define FLOW_COLS 8
#define FLOW_ROWS 24

/* ---- shaders ----------------------------------------------------------- */

static const char *flow_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec2 uv;\n"  /* normalized 0..1 (x across, y down) */
    "layout(location = 2) in float turb;\n" /* per-vertex turbulence (steepness) */
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "out vec2 vUV;\n"
    "out float vTurb;\n"
    "out vec3 vWorldPos;\n"
    "void main() {\n"
    "    vUV = uv;\n"
    "    vTurb = turb;\n"
    "    vWorldPos = position;\n"
    "    gl_Position = uProjection * uView * vec4(position, 1.0);\n"
    "}\n";

static const char *flow_frag =
    "#version 330 core\n"
    "in vec2 vUV;\n"                  /* x across 0..1, y DOWN the fall 0..1 */
    "in float vTurb;\n"
    "in vec3 vWorldPos;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uHasTex;\n"
    "uniform float uTime;\n"
    "uniform float uSpeed;\n"
    "uniform float uTiling;\n"
    "uniform vec3 uColor;\n"          /* deep water colour */
    "uniform vec3 uCameraPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uLightColor;\n"
    "uniform int uClipOn;\n"
    "uniform float uClipY;\n"
    "uniform float uFoam;\n"          /* multiplicador de espuma por cascada */
    "out vec4 FragColor;\n"
    /* --- ruido de valor procedural (no depende de textura) --- */
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }\n"
    "float vnoise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  float a=hash(i), b=hash(i+vec2(1,0)), c=hash(i+vec2(0,1)), d=hash(i+vec2(1,1));\n"
    "  return mix(mix(a,b,f.x), mix(c,d,f.x), f.y); }\n"
    "float fbm(vec2 p){ float s=0.0, a=0.5; for(int i=0;i<3;i++){ s+=a*vnoise(p); p*=2.0; a*=0.5;} return s; }\n"
    "void main() {\n"
    "    if (uClipOn == 1 && vWorldPos.y < uClipY) discard;\n"
    "    float across = vUV.x;\n"
    "    float down = vUV.y;\n"
    "    float sc = max(uTiling, 1.0);\n"
    "    float t = uTime * max(uSpeed, 0.5);\n"
    // Chorros VERTICALES que caen: ruido estirado en Y, desplazandose rapido hacia abajo.
    "    float j1 = fbm(vec2(across*7.0, down*sc*1.6 - t*3.0));\n"
    "    float j2 = fbm(vec2(across*15.0 + 3.7, down*sc*3.0 - t*5.0));\n"
    "    float streak = mix(j1, j2, 0.5);\n"
    // Textura propia de la cascada (opcional): se desplaza cayendo y aporta detalle
    "    vec3 texCol = vec3(1.0);\n"
    "    if (uHasTex == 1) {\n"
    "        vec3 tx = texture(uTex, vec2(across*2.0, down*sc - t*2.0)).rgb;\n"
    "        streak = mix(streak, dot(tx, vec3(0.333)), 0.5);\n"
    "        texCol = tx;\n"
    "    }\n"
    // Lineas verticales finas (los hilos de agua) moduladas por el ruido.
    "    float threads = 0.5 + 0.5*sin(across*46.0 + j1*7.0);\n"
    // --- Borde ROTO: recorta los lados con ondulacion, para que NO sea una cinta plana ---
    "    float wob = fbm(vec2(down*sc*2.2 - t*3.5, across*3.0)) * 0.16;\n"
    "    float ex = abs(across - 0.5) + wob;\n"
    "    if (ex > 0.5) discard;\n"                                   // silueta irregular
    "    float side = smoothstep(0.5, 0.4, ex);\n"                   // funde los lados
    // --- Espuma: arriba (donde rompe el borde), abajo (impacto) y por turbulencia ---
    "    float topFoam  = smoothstep(0.14, 0.0, down);\n"
    "    float baseFoam = smoothstep(0.72, 1.0, down);\n"
    "    float turbFoam = smoothstep(0.5, 1.0, vTurb) * 0.6;\n"
    "    float foam = max(max(topFoam, baseFoam), turbFoam);\n"
    "    foam *= (0.45 + 0.75*streak) * max(uFoam, 0.0);\n"
    "    foam = clamp(foam, 0.0, 1.0);\n"
    // --- Color: agua profunda -> clara, y a blanco en la espuma ---
    "    vec3 deep = uColor;\n"
    "    vec3 shallow = clamp(uColor*1.6 + 0.15, 0.0, 1.0);\n"
    "    vec3 water = mix(deep, shallow, 0.35 + 0.5*streak);\n"
    "    water *= (0.8 + 0.35*threads);\n"                           // hilos de agua
    "    if (uHasTex == 1) water = mix(water, water * texCol * 1.6, 0.5);\n"  // tinte de la textura
    "    vec3 col = mix(water, vec3(1.0), foam);\n"
    // brillo del sol en las crestas de los chorros
    "    col += uLightColor * pow(clamp(streak,0.0,1.0), 6.0) * 0.35;\n"
    // --- Alfa: cuerpo semitransparente, opaco en la espuma, con hilos ---
    "    float alpha = (0.5 + 0.45*streak) * side;\n"
    "    alpha = max(alpha, foam * 0.95);\n"
    "    FragColor = vec4(col, clamp(alpha, 0.0, 1.0));\n"
    "}\n";

/* ---- state ------------------------------------------------------------- */

typedef struct {
    unsigned int vao, vbo, ebo;
    int index_count;
    float speed;
    float tiling;
    /* estilo PROPIO de la cascada (capturado del global al crearla) */
    float color[3];
    unsigned int tex;
    float foam;
    int active;
} FlowQuad;

static struct {
    int initialized;
    G3DShaderProgram *shader;
    unsigned int tex_handle;
    float color[3];
    float foam;             /* multiplicador de espuma actual (para capturar) */
    float speed_mul;        /* multiplicador de velocidad actual */
    int clip_on;
    float clip_y;
    FlowQuad quads[G3D_MAX_FLOWS];
    int count;
} g_flow = {0};

void g3d_flow_set_clip(float y) {
    g_flow.clip_on = 1;
    g_flow.clip_y = y;
}

static void flow_lazy_init(void) {
    if (g_flow.initialized)
        return;
    g_flow.shader = g3d_shader_create(flow_vert, flow_frag);
    g_flow.color[0] = 0.6f; g_flow.color[1] = 0.78f; g_flow.color[2] = 0.85f;
    g_flow.foam = 1.0f; g_flow.speed_mul = 1.0f;
    g_flow.initialized = 1;
}

void g3d_flow_set_texture(unsigned int gl_handle) {
    g_flow.tex_handle = gl_handle;
}

void g3d_flow_set_color(float r, float g, float b) {
    g_flow.color[0] = r; g_flow.color[1] = g; g_flow.color[2] = b;
}

/* Intensidad de espuma y multiplicador de velocidad de la PROXIMA cascada. */
void g3d_flow_set_foam(float foam) { g_flow.foam = foam; }
void g3d_flow_set_speed(float mul) { g_flow.speed_mul = mul > 0.0f ? mul : 1.0f; }

#ifndef VITA
/* Upload an interleaved (xyz, uv, turb) vertex grid as a flow quad. Returns id. */
static int flow_register(const float *vdata, int vcount, int cols, int rows,
                         float speed, float tiling) {
    if (g_flow.count >= G3D_MAX_FLOWS)
        return -1;
    int vcols = cols + 1;
    int icount = cols * rows * 6;
    unsigned short *idata =
        (unsigned short *)malloc((size_t)icount * sizeof(unsigned short));
    if (!idata)
        return -1;
    int t = 0;
    for (int j = 0; j < rows; j++) {
        for (int i = 0; i < cols; i++) {
            unsigned short a = (unsigned short)(j * vcols + i);
            unsigned short b = (unsigned short)(j * vcols + i + 1);
            unsigned short c = (unsigned short)((j + 1) * vcols + i);
            unsigned short d = (unsigned short)((j + 1) * vcols + i + 1);
            idata[t++] = a; idata[t++] = c; idata[t++] = b;
            idata[t++] = b; idata[t++] = c; idata[t++] = d;
        }
    }

    const int stride = 6; /* xyz, uv, turb */
    FlowQuad *q = &g_flow.quads[g_flow.count];
    glGenVertexArrays(1, &q->vao);
    glBindVertexArray(q->vao);
    glGenBuffers(1, &q->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, q->vbo);
    glBufferData(GL_ARRAY_BUFFER, (long)(vcount * stride * sizeof(float)), vdata,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                          (void *)(5 * sizeof(float)));
    glGenBuffers(1, &q->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, q->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)(icount * sizeof(unsigned short)),
                 idata, GL_STATIC_DRAW);
    glBindVertexArray(0);

    q->index_count = icount;
    q->speed = speed * (g_flow.speed_mul > 0.0f ? g_flow.speed_mul : 1.0f);
    q->tiling = tiling;
    /* captura el estilo actual como propio de esta cascada */
    q->color[0] = g_flow.color[0]; q->color[1] = g_flow.color[1]; q->color[2] = g_flow.color[2];
    q->tex = g_flow.tex_handle;
    q->foam = g_flow.foam > 0.0f ? g_flow.foam : 1.0f;
    q->active = 1;
    free(idata);
    return g_flow.count++;
}
#endif

int g3d_flow_add(float tx, float ty, float tz, float bx, float by, float bz,
                 float width, float speed, float tiling) {
    flow_lazy_init();
    if (!g_flow.shader)
        return -1;
#ifndef VITA
    int cols = FLOW_COLS, rows = FLOW_ROWS;
    int vcols = cols + 1, vrows = rows + 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;

    /* Free-falling sheet: turbulence from how vertical the drop is */
    float dropy = fabsf(ty - by);
    float horiz = sqrtf((bx - tx) * (bx - tx) + (bz - tz) * (bz - tz)) + 0.001f;
    float turb = dropy / (dropy + horiz);     /* 0 flat .. 1 vertical */
    turb = 0.35f + 0.65f * turb;

    float hw = width * 0.5f;
    /* Direccion HORIZONTAL de la caida (hacia donde vuela el agua) y su
       perpendicular (el ancho de la lamina). El ancho va perpendicular a la caida
       (no siempre en X), y la lamina se EMPUJA hacia adelante segun baja, para que
       cuelgue DELANTE del talud y el terreno no la tape. */
    float dxh = bx - tx, dzh = bz - tz;
    float hlen = sqrtf(dxh*dxh + dzh*dzh);
    float hx, hz;
    if (hlen > 0.05f) { hx = dxh/hlen; hz = dzh/hlen; }
    else { hx = 0.0f; hz = 1.0f; }          /* caida vertical pura: orientacion por defecto */
    float px = -hz, pz = hx;                  /* perpendicular en XZ = ancho */
    /* Separacion del talud: CONSTANTE (todo el chorro delante del acantilado, no
       solo la base) + un poco mas al caer, para que el hombro redondeado del borde
       no tape la parte de arriba (ese era el "corte"). */
    float fwd = width * 0.5f + 1.0f;
    int k = 0;
    for (int j = 0; j < vrows; j++) {
        float fv = (float)j / (float)rows;
        float cx = tx + (bx - tx) * fv;
        float cy = ty + (by - ty) * fv;
        float cz = tz + (bz - tz) * fv;
        float pushf = fwd * (0.55f + 0.45f * fv);   /* algo de separacion ya arriba */
        cx += hx * pushf;
        cz += hz * pushf;
        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            vdata[k++] = cx + px * ox;
            vdata[k++] = cy;
            vdata[k++] = cz + pz * ox;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    return -1;
#endif
}

int g3d_flow_add_river(void *terrain_mesh, float x0, float z0, float x1,
                       float z1, float width, float y_offset, float speed,
                       float tiling) {
    flow_lazy_init();
    if (!g_flow.shader || !terrain_mesh)
        return -1;
#ifndef VITA
    G3DMesh *terrain = (G3DMesh *)terrain_mesh;
    int cols = FLOW_COLS, rows = FLOW_ROWS * 2; /* more rows: follow contours */
    int vcols = cols + 1, vrows = rows + 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;

    /* Direction along the river (XZ) and the perpendicular for the width */
    float dx = x1 - x0, dz = z1 - z0;
    float dlen = sqrtf(dx * dx + dz * dz);
    if (dlen < 1e-4f) dlen = 1e-4f;
    dx /= dlen; dz /= dlen;
    float px = -dz, pz = dx; /* perpendicular in XZ */
    float hw = width * 0.5f;
    float seg = dlen / (float)rows; /* world length between rows */

    int k = 0;
    for (int j = 0; j < vrows; j++) {
        float fv = (float)j / (float)rows;
        float cx = x0 + (x1 - x0) * fv;
        float cz = z0 + (z1 - z0) * fv;

        /* Local slope along the flow at this row -> turbulence (steep = foam) */
        float fa = (j > 0) ? (float)(j - 1) / (float)rows : 0.0f;
        float fb = (j < rows) ? (float)(j + 1) / (float)rows : 1.0f;
        float ya = g3d_terrain_get_height(terrain, x0 + (x1 - x0) * fa,
                                          z0 + (z1 - z0) * fa);
        float yb = g3d_terrain_get_height(terrain, x0 + (x1 - x0) * fb,
                                          z0 + (z1 - z0) * fb);
        float slope = fabsf(yb - ya) / (2.0f * seg + 1e-4f);
        float turb = slope / (slope + 0.6f); /* 0 flat .. ->1 steep */

        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            float wx = cx + px * ox;
            float wz = cz + pz * ox;
            /* Conform to the terrain surface: sample its height + offset */
            float wy = g3d_terrain_get_height(terrain, wx, wz) + y_offset;
            vdata[k++] = wx;
            vdata[k++] = wy;
            vdata[k++] = wz;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    return -1;
#endif
}

int g3d_flow_add_path(const float *pts, int n, float width, float y_offset,
                      float speed, float tiling) {
    flow_lazy_init();
    if (!g_flow.shader || !pts || n < 2)
        return -1;
#ifndef VITA
    int cols = FLOW_COLS;
    int vcols = cols + 1, vrows = n, rows = n - 1;
    int vcount = vcols * vrows;
    float *vdata = (float *)malloc((size_t)vcount * 6 * sizeof(float));
    if (!vdata)
        return -1;
    float hw = width * 0.5f;

    /* total length for normalized UV.v along the river */
    float total = 0.0f;
    for (int j = 0; j < n - 1; j++) {
        float ddx = pts[(j + 1) * 3] - pts[j * 3];
        float ddz = pts[(j + 1) * 3 + 2] - pts[j * 3 + 2];
        total += sqrtf(ddx * ddx + ddz * ddz);
    }
    if (total < 1e-4f) total = 1e-4f;

    float cum = 0.0f;
    int k = 0;
    for (int j = 0; j < n; j++) {
        float cx = pts[j * 3], cy = pts[j * 3 + 1], cz = pts[j * 3 + 2];
        /* flow direction = average of the adjacent segments (smooth bends) */
        float dx = 0.0f, dz = 0.0f;
        if (j > 0)     { dx += cx - pts[(j - 1) * 3]; dz += cz - pts[(j - 1) * 3 + 2]; }
        if (j < n - 1) { dx += pts[(j + 1) * 3] - cx; dz += pts[(j + 1) * 3 + 2] - cz; }
        float dl = sqrtf(dx * dx + dz * dz); if (dl < 1e-4f) dl = 1e-4f;
        dx /= dl; dz /= dl;
        float px = -dz, pz = dx;  /* perpendicular in XZ */

        float turb = 0.2f;
        if (j > 0) {
            float dy = fabsf(cy - pts[(j - 1) * 3 + 1]);
            float sx = cx - pts[(j - 1) * 3], sz = cz - pts[(j - 1) * 3 + 2];
            float seg = sqrtf(sx * sx + sz * sz) + 1e-4f;
            float slope = dy / seg;
            turb = slope / (slope + 0.6f);
            cum += seg;
        }
        float fv = cum / total;

        for (int i = 0; i < vcols; i++) {
            float fu = (float)i / (float)cols;
            float ox = (fu - 0.5f) * 2.0f * hw;
            vdata[k++] = cx + px * ox;
            vdata[k++] = cy + y_offset;
            vdata[k++] = cz + pz * ox;
            vdata[k++] = fu;
            vdata[k++] = fv;
            vdata[k++] = turb;
        }
    }
    int id = flow_register(vdata, vcount, cols, rows, speed, tiling);
    free(vdata);
    return id;
#else
    (void)pts; (void)n; (void)width; (void)y_offset; (void)speed; (void)tiling;
    return -1;
#endif
}

/* Desde el borde de una caida, sigue el terreno CUESTA ABAJO (maxima pendiente)
   hasta la base del acantilado (donde se aplana) o hasta el agua. Asi la cascada
   llega hasta abajo aunque el rio solo se asome al borde. Devuelve el pie. */
static void trace_fall_base(const float *H, int side, float ws, float width,
                            float x, float y, float z,
                            float *ox, float *oy, float *oz) {
    float st = width * 0.4f; if (st < 0.5f) st = 0.5f;
    float cx = x, cy = y, cz = z;
    for (int k = 0; k < 150; k++) {
        /* si hay agua por encima del terreno aqui, la caida aterriza en ella */
        float wl = g3d_water_level_at(cx, cz);
        if (wl > cy) { cy = wl; break; }
        /* busca la direccion (8) que mas baja */
        float bestDrop = 0.0f, bnx = cx, bnz = cz, bny = cy;
        for (int d = 0; d < 8; d++) {
            float a = (float)d * 0.7853982f;   /* 45 deg */
            float nx = cx + cosf(a) * st, nz = cz + sinf(a) * st;
            float ny = g3d_heightfield_height(H, side, ws, nx, nz);
            float drop = cy - ny;
            if (drop > bestDrop) { bestDrop = drop; bnx = nx; bnz = nz; bny = ny; }
        }
        if (bestDrop < st * 0.12f) break;   /* ya casi plano (~7 deg) -> base del acantilado */
        cx = bnx; cy = bny; cz = bnz;
    }
    *ox = cx; *oy = cy; *oz = cz;
}

void g3d_river_add_waterfalls(const float *pts, int n, const float *H,
                              int side, float ws, float width) {
    if (!pts || n < 2 || !H) return;
    /* Muestrea DENSO a lo largo del cauce (no solo entre los puntos clicados, que
       pueden estar lejos): asi se detecta un acantilado donde sea que el rio lo
       cruce. En cada tramo con pendiente fuerte se anade una lamina de agua cayendo
       (g3d_flow_add), que el paso de flujo dibuja con espuma segun la verticalidad. */
    float step = width * 0.6f; if (step < 1.0f) step = 1.0f;
    /* Recorre DENSO y acumula cada tramo de caida CONTIGUO en UNA sola lamina (del
       borde superior al pie), en vez de muchos quads sueltos (que dejaban recortes).
       El pie aterriza en la SUPERFICIE DEL AGUA si hay lago/rio abajo, para que se
       mezcle bien; y se pone una honda de salpicadura ahi. */
    int inRun = 0;
    float topx = 0, topy = 0, topz = 0;         /* borde superior de la caida */
    float px = 0, py = 0, pz = 0;               /* punto denso anterior */
    int first = 1;
    for (int i = 0; i < n; i++) {
        float ax = pts[i * 3], az = pts[i * 3 + 2];
        float bx, bz; int sub;
        if (i < n - 1) {
            bx = pts[(i + 1) * 3]; bz = pts[(i + 1) * 3 + 2];
            float dx = bx - ax, dz = bz - az;
            sub = (int)(sqrtf(dx*dx + dz*dz) / step); if (sub < 1) sub = 1;
        } else { bx = ax; bz = az; sub = 0; }
        for (int s = (i == 0 ? 0 : 1); s <= sub; s++) {
            float t = (sub > 0) ? (float)s / sub : 0.0f;
            float x = ax + (bx - ax) * t, z = az + (bz - az) * t;
            float y = g3d_heightfield_height(H, side, ws, x, z);
            if (!first) {
                float drop = py - y;
                float horiz = sqrtf((x-px)*(x-px) + (z-pz)*(z-pz)) + 1e-4f;
                int steep = (drop > 1.2f && drop > horiz * 0.6f);
                if (steep && !inRun) { inRun = 1; topx = px; topy = py; topz = pz; }
                if (!steep && inRun) {
                    /* fin de la caida: desde el BORDE (topx) sigue el acantilado
                       cuesta abajo hasta la BASE real y emite UNA lamina. */
                    float bx2, by2, bz2;
                    trace_fall_base(H, side, ws, width, topx, topy, topz, &bx2, &by2, &bz2);
                    float dropT = topy - by2; float til = dropT * 0.4f; if (til < 1.0f) til = 1.0f;
                    g3d_flow_add(topx, topy + 0.3f, topz, bx2, by2, bz2, width, 2.5f, til);
                    g3d_water_add_ripple_source(bx2, bz2, 1.3f);
                    inRun = 0;
                }
            }
            px = x; py = y; pz = z; first = 0;
        }
    }
    if (inRun) {   /* la caida sigue hasta el final del rio: traza hasta la base */
        float bx2, by2, bz2;
        trace_fall_base(H, side, ws, width, topx, topy, topz, &bx2, &by2, &bz2);
        float dropT = topy - by2; float til = dropT * 0.4f; if (til < 1.0f) til = 1.0f;
        g3d_flow_add(topx, topy + 0.3f, topz, bx2, by2, bz2, width, 2.5f, til);
        g3d_water_add_ripple_source(bx2, bz2, 1.3f);
    }
}

void g3d_flow_render_pass(G3DCamera *camera, int flip_y) {
    if (!g_flow.initialized || !g_flow.shader || g_flow.count == 0 || !camera)
        return;

#ifndef VITA
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }
    float time = (float)SDL_GetTicks() / 1000.0f;

    g3d_shader_use(g_flow.shader);
    g3d_shader_set_mat4(g_flow.shader, "uView", view);
    g3d_shader_set_mat4(g_flow.shader, "uProjection", proj);
    g3d_shader_set_float(g_flow.shader, "uTime", time);
    g3d_shader_set_vec3(g_flow.shader, "uColor",
                        vec3_make(g_flow.color[0], g_flow.color[1], g_flow.color[2]));
    g3d_shader_set_int(g_flow.shader, "uClipOn", g_flow.clip_on);
    g3d_shader_set_float(g_flow.shader, "uClipY", g_flow.clip_y);
    g3d_shader_set_vec3(g_flow.shader, "uCameraPos", camera->position);

    /* Sun direction / colour from the active scene's directional light */
    Vec3 ldir = vec3_make(-0.5f, -1.0f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 1.0f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction;
            lcol = vec3_make(l->color[0], l->color[1], l->color[2]);
            break;
        }
    }
    g3d_shader_set_vec3(g_flow.shader, "uLightDir", ldir);
    g3d_shader_set_vec3(g_flow.shader, "uLightColor", lcol);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    for (int i = 0; i < g_flow.count; i++) {
        FlowQuad *q = &g_flow.quads[i];
        if (!q->active)
            continue;
        g3d_shader_set_float(g_flow.shader, "uSpeed", q->speed);
        g3d_shader_set_float(g_flow.shader, "uTiling", q->tiling);
        g3d_shader_set_float(g_flow.shader, "uFoam", q->foam);
        g3d_shader_set_vec3(g_flow.shader, "uColor",
                            vec3_make(q->color[0], q->color[1], q->color[2]));
        if (q->tex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, q->tex);
            g3d_shader_set_int(g_flow.shader, "uTex", 0);
            g3d_shader_set_int(g_flow.shader, "uHasTex", 1);
        } else {
            g3d_shader_set_int(g_flow.shader, "uHasTex", 0);
        }
        glBindVertexArray(q->vao);
        glDrawElements(GL_TRIANGLES, q->index_count, GL_UNSIGNED_SHORT, 0);
    }
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

void g3d_flow_clear(void) {
#ifndef VITA
    for (int i = 0; i < g_flow.count; i++) {
        FlowQuad *q = &g_flow.quads[i];
        if (q->vao) glDeleteVertexArrays(1, &q->vao);
        if (q->vbo) glDeleteBuffers(1, &q->vbo);
        if (q->ebo) glDeleteBuffers(1, &q->ebo);
        q->active = 0;
        q->vao = q->vbo = q->ebo = 0;
    }
#endif
    g_flow.count = 0;
}

void g3d_flow_shutdown(void) {
    g3d_flow_clear();
    if (g_flow.shader) {
        g3d_shader_free(g_flow.shader);
        g_flow.shader = NULL;
    }
    g_flow.initialized = 0;
}
