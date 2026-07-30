/*
 * libmod_3d_water_spectrum.c - Spectral (FFT) ocean waves on the GPU
 *
 * See the header for what this is and why. The pipeline per cascade per frame:
 * advance the base spectrum to time t, inverse-FFT it (log2(N) butterfly passes
 * along rows then columns), and assemble the displacement and derivative maps.
 *
 * The base spectrum h0 depends only on the wind, so it is generated once and
 * reused until the wind changes. The ping-pong scratch buffers are shared by all
 * cascades because the cascades are processed one after another.
 */

#include "libmod_3d_water_spectrum.h"
#include "libmod_3d_water_spectrum_glsl.h"
#include "libmod_3d_glcaps.h"
#include "libmod_3d_shader.h"
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

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY  0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#endif

#define TAU 6.283185307179586f

static struct {
    int ready;
    int N, stages;

    G3DShaderProgram *cs_butterfly, *cs_h0, *cs_spectrum, *cs_fft, *cs_assemble;

    unsigned int butterfly;
    unsigned int h0[G3D_WATER_CASCADES];
    unsigned int ppA[2], ppB[2];      /* shared scratch, ping-ponged */
    unsigned int displacement, derivative;   /* 2D array textures, one layer per cascade */

    float tile[G3D_WATER_CASCADES];
    float band[G3D_WATER_CASCADES + 1];

    float wind_x, wind_z, wind_speed, fetch;
    float amplitude, choppy;
    int   spectrum_dirty;
} S = {
    .wind_x = 1.0f, .wind_z = 0.25f, .wind_speed = 14.0f, .fetch = 1.0f,
    .amplitude = 1.0f, .choppy = 1.0f,
};

/* --------------------------------------------------------------------------
   Setup
   -------------------------------------------------------------------------- */

static unsigned int tex2d(int n, unsigned int fmt) {
    unsigned int t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, n, n, 0, GL_RGBA, GL_FLOAT, NULL);
    return t;
}

static unsigned int tex2d_array(int n, int layers, unsigned int fmt) {
    unsigned int t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D_ARRAY, t);
    /* Mip-mapped on purpose. Sampled at level 0 from far away, a wave field
       aliases badly: each pixel lands on an arbitrary wave and the surface
       crawls with shimmer. The mip chain averages the waves the pixel actually
       covers, which is the only way a distant sea reads as calm rather than as
       noise. */
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    /* EVERY mip level has to be allocated, not just level 0. Asking for a
       mip-mapped filter on a texture that only has level 0 leaves it INCOMPLETE,
       and an incomplete texture does not merely sample oddly -- glGetTexImage
       hands back garbage, which looks exactly like a broken compute shader and
       sends you hunting in the wrong place entirely. glGenerateMipmap fills the
       levels each frame; this only reserves them. */
    int levels = 0;
    for (int s = n; s >= 1; s >>= 1) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, levels, (GLint)fmt, s, s, layers, 0,
                     GL_RGBA, GL_FLOAT, NULL);
        levels++;
        if (s == 1) break;
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels - 1);
    return t;
}

static void spectrum_free_gl(void) {
    if (S.butterfly) { glDeleteTextures(1, &S.butterfly); S.butterfly = 0; }
    for (int i = 0; i < G3D_WATER_CASCADES; i++)
        if (S.h0[i]) { glDeleteTextures(1, &S.h0[i]); S.h0[i] = 0; }
    for (int i = 0; i < 2; i++) {
        if (S.ppA[i]) { glDeleteTextures(1, &S.ppA[i]); S.ppA[i] = 0; }
        if (S.ppB[i]) { glDeleteTextures(1, &S.ppB[i]); S.ppB[i] = 0; }
    }
    if (S.displacement) { glDeleteTextures(1, &S.displacement); S.displacement = 0; }
    if (S.derivative)   { glDeleteTextures(1, &S.derivative);   S.derivative = 0; }
}

static char *cs_src(const char *body) {
    const G3DGLCaps *c = g3d_glcaps();
    const char *ver = c->es ? "#version 310 es\nprecision highp float;\n"
                              "precision highp image2D;\nprecision highp image2DArray;\n"
                            : "#version 430\n";
    size_t n = strlen(ver) + strlen(body) + 1;
    char *s = (char *)malloc(n);
    if (!s) return NULL;
    strcpy(s, ver); strcat(s, body);
    return s;
}

static G3DShaderProgram *build_cs(const char *body, const char *label) {
    char *src = cs_src(body);
    if (!src) return NULL;
    G3DShaderProgram *p = g3d_shader_create_compute(src);
    free(src);
    if (!p) fprintf(stderr, "G3D: water spectrum: %s failed to build\n", label);
    return p;
}

/* The butterfly table depends only on N, so it is generated once at init. */
static void build_butterfly(void) {
    g3d_shader_use(S.cs_butterfly);
    g3d_shader_set_int(S.cs_butterfly, "uN", S.N);
    g3d_shader_set_int(S.cs_butterfly, "uStages", S.stages);
    glBindImageTexture(0, S.butterfly, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    g3d_shader_dispatch(S.cs_butterfly, S.stages, S.N / 16, 1);
    g3d_shader_image_barrier();
}

/* Regenerate every cascade's base spectrum. Only needed when the wind changes. */
static void build_h0(void) {
    g3d_shader_use(S.cs_h0);
    float wl = sqrtf(S.wind_x * S.wind_x + S.wind_z * S.wind_z);
    float wx = wl > 1e-5f ? S.wind_x / wl : 1.0f;
    float wz = wl > 1e-5f ? S.wind_z / wl : 0.0f;
    g3d_shader_set_int(S.cs_h0, "uN", S.N);
    g3d_shader_set_vec2(S.cs_h0, "uWindDir", wx, wz);
    g3d_shader_set_float(S.cs_h0, "uWindSpeed", S.wind_speed);
    /* Calibrated so amplitude = 1 gives roughly 0.6 units RMS wave height -- a
       moderate breeze-driven sea. The Phillips constant has no natural scale of
       its own, so this is the number that decides whether the default looks like
       a lake or a hurricane; test_water_fft.c pins the resulting RMS to a range
       so it cannot drift unnoticed. */
    g3d_shader_set_float(S.cs_h0, "uAmplitude", S.amplitude * 1.6e-7f);
    g3d_shader_set_float(S.cs_h0, "uFetch", S.fetch);

    for (int c = 0; c < G3D_WATER_CASCADES; c++) {
        g3d_shader_set_float(S.cs_h0, "uTileSize", S.tile[c]);
        g3d_shader_set_vec2(S.cs_h0, "uBandLimit", S.band[c], S.band[c + 1]);
        g3d_shader_set_int(S.cs_h0, "uSeed", 1013 + c * 7717);
        glBindImageTexture(0, S.h0[c], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        g3d_shader_dispatch(S.cs_h0, S.N / 16, S.N / 16, 1);
    }
    g3d_shader_image_barrier();
    S.spectrum_dirty = 0;
}

int g3d_water_spectrum_init(int resolution) {
    if (S.ready) return 1;
    if (!g3d_glcaps()->compute) return 0;

    int n = resolution;
    if (n < 32) n = 32;
    /* Round down to a power of two: the butterfly FFT requires it. */
    int p = 1, stages = 0;
    while (p * 2 <= n) { p *= 2; stages++; }
    n = p;

    S.N = n;
    S.stages = stages;

    S.cs_butterfly = build_cs(g3d_wspec_glsl_butterfly, "butterfly");
    S.cs_h0        = build_cs(g3d_wspec_glsl_h0,        "h0");
    S.cs_spectrum  = build_cs(g3d_wspec_glsl_spectrum,  "spectrum");
    S.cs_fft       = build_cs(g3d_wspec_glsl_fft,       "fft");
    S.cs_assemble  = build_cs(g3d_wspec_glsl_assemble,  "assemble");
    if (!S.cs_butterfly || !S.cs_h0 || !S.cs_spectrum || !S.cs_fft || !S.cs_assemble) {
        g3d_water_spectrum_shutdown();
        return 0;
    }

    /* Cascade tiles, largest to smallest. The bands below hand each cascade an
       exclusive slice of the spectrum; overlapping them would count the same
       waves twice and double their height. */
    S.tile[0] = 400.0f;
    S.tile[1] = 90.0f;
    S.tile[2] = 18.0f;
    S.band[0] = 0.0f;
    S.band[1] = TAU * 6.0f / S.tile[1];
    S.band[2] = TAU * 6.0f / S.tile[2];
    S.band[3] = 1.0e9f;

    S.butterfly = 0;
    glGenTextures(1, &S.butterfly);
    glBindTexture(GL_TEXTURE_2D, S.butterfly);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, S.stages, S.N, 0, GL_RGBA, GL_FLOAT, NULL);

    for (int c = 0; c < G3D_WATER_CASCADES; c++) S.h0[c] = tex2d(S.N, GL_RGBA32F);
    for (int i = 0; i < 2; i++) {
        S.ppA[i] = tex2d(S.N, GL_RGBA32F);
        S.ppB[i] = tex2d(S.N, GL_RGBA32F);
    }
    S.displacement = tex2d_array(S.N, G3D_WATER_CASCADES, GL_RGBA16F);
    S.derivative   = tex2d_array(S.N, G3D_WATER_CASCADES, GL_RGBA16F);

    build_butterfly();
    build_h0();

    S.ready = 1;
    printf("G3D: spectral ocean ready (%d^2, %d cascades, %d FFT stages)\n",
           S.N, G3D_WATER_CASCADES, S.stages);
    return 1;
}

int g3d_water_spectrum_ready(void) { return S.ready; }

/* --------------------------------------------------------------------------
   Parameters
   -------------------------------------------------------------------------- */

void g3d_water_spectrum_set_wind(float dir_x, float dir_z, float speed,
                                 float fetch_scale) {
    if (speed < 0.1f) speed = 0.1f;
    if (fetch_scale < 0.05f) fetch_scale = 0.05f;
    if (fetch_scale > 1.0f) fetch_scale = 1.0f;
    if (dir_x == S.wind_x && dir_z == S.wind_z &&
        speed == S.wind_speed && fetch_scale == S.fetch)
        return;                       /* regenerating h0 is not free */
    S.wind_x = dir_x; S.wind_z = dir_z;
    S.wind_speed = speed; S.fetch = fetch_scale;
    S.spectrum_dirty = 1;
}

void g3d_water_spectrum_set_amplitude(float amplitude, float choppiness) {
    if (amplitude < 0.0f) amplitude = 0.0f;
    if (choppiness < 0.0f) choppiness = 0.0f;
    if (choppiness > 2.0f) choppiness = 2.0f;
    if (amplitude != S.amplitude) { S.amplitude = amplitude; S.spectrum_dirty = 1; }
    S.choppy = choppiness;            /* applied at assemble time: no rebuild */
}

void g3d_water_spectrum_tile_sizes(float *out) {
    if (!out) return;
    for (int c = 0; c < G3D_WATER_CASCADES; c++) out[c] = S.tile[c];
}

/* --------------------------------------------------------------------------
   Per-frame update
   -------------------------------------------------------------------------- */

/* Run log2(N) butterfly passes over one ping-pong pair, in one direction.
   Returns the index of the buffer holding the result. */
static int fft_direction(unsigned int *pp, int start, int vertical) {
    int src = start;
    for (int stage = 0; stage < S.stages; stage++) {
        g3d_shader_set_int(S.cs_fft, "uStage", stage);
        g3d_shader_set_int(S.cs_fft, "uVertical", vertical);
        glBindImageTexture(0, pp[src],     0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA32F);
        glBindImageTexture(1, pp[1 - src], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        g3d_shader_dispatch(S.cs_fft, S.N / 16, S.N / 16, 1);
        /* Each pass reads what the previous one wrote, so they cannot overlap. */
        g3d_shader_image_barrier();
        src = 1 - src;
    }
    return src;
}

void g3d_water_spectrum_update(float t) {
    if (!S.ready) return;
    if (S.spectrum_dirty) build_h0();

    for (int c = 0; c < G3D_WATER_CASCADES; c++) {
        /* 1. advance the spectrum to time t */
        g3d_shader_use(S.cs_spectrum);
        g3d_shader_set_int(S.cs_spectrum, "uN", S.N);
        g3d_shader_set_float(S.cs_spectrum, "uTileSize", S.tile[c]);
        g3d_shader_set_float(S.cs_spectrum, "uTime", t);
        glBindImageTexture(0, S.h0[c],  0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA32F);
        glBindImageTexture(1, S.ppA[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glBindImageTexture(2, S.ppB[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        g3d_shader_dispatch(S.cs_spectrum, S.N / 16, S.N / 16, 1);
        g3d_shader_image_barrier();

        /* 2. inverse FFT: rows, then columns, for both packed field pairs */
        g3d_shader_use(S.cs_fft);
        g3d_shader_set_int(S.cs_fft, "uN", S.N);
        glBindImageTexture(2, S.butterfly, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        int ia = fft_direction(S.ppA, 0, 0);
        ia = fft_direction(S.ppA, ia, 1);
        int ib = fft_direction(S.ppB, 0, 0);
        ib = fft_direction(S.ppB, ib, 1);

        /* 3. unshift, normalise, write this cascade's layer */
        g3d_shader_use(S.cs_assemble);
        g3d_shader_set_int(S.cs_assemble, "uN", S.N);
        g3d_shader_set_int(S.cs_assemble, "uLayer", c);
        g3d_shader_set_float(S.cs_assemble, "uTileSize", S.tile[c]);
        g3d_shader_set_float(S.cs_assemble, "uChoppy", S.choppy);
        glBindImageTexture(0, S.ppA[ia], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, S.ppB[ib], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(2, S.displacement, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(3, S.derivative,   0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        g3d_shader_dispatch(S.cs_assemble, S.N / 16, S.N / 16, 1);
        g3d_shader_image_barrier();
    }

    /* Rebuild the mip chains from the new level 0. Without this the distance
       LOD would keep sampling last frame's -- or an empty -- mip. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, S.displacement);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, S.derivative);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

int g3d_water_spectrum_bind(int unit_displacement, int unit_derivative) {
    if (!S.ready) return 0;
    glActiveTexture(GL_TEXTURE0 + unit_displacement);
    glBindTexture(GL_TEXTURE_2D_ARRAY, S.displacement);
    glActiveTexture(GL_TEXTURE0 + unit_derivative);
    glBindTexture(GL_TEXTURE_2D_ARRAY, S.derivative);
    glActiveTexture(GL_TEXTURE0);
    return G3D_WATER_CASCADES;
}

void g3d_water_spectrum_shutdown(void) {
    if (S.cs_butterfly) { g3d_shader_free(S.cs_butterfly); S.cs_butterfly = NULL; }
    if (S.cs_h0)        { g3d_shader_free(S.cs_h0);        S.cs_h0 = NULL; }
    if (S.cs_spectrum)  { g3d_shader_free(S.cs_spectrum);  S.cs_spectrum = NULL; }
    if (S.cs_fft)       { g3d_shader_free(S.cs_fft);       S.cs_fft = NULL; }
    if (S.cs_assemble)  { g3d_shader_free(S.cs_assemble);  S.cs_assemble = NULL; }
    spectrum_free_gl();
    S.ready = 0;
}

#else  /* VITA: no compute */

int  g3d_water_spectrum_init(int r) { (void)r; return 0; }
int  g3d_water_spectrum_ready(void) { return 0; }
void g3d_water_spectrum_set_wind(float a, float b, float c, float d) { (void)a; (void)b; (void)c; (void)d; }
void g3d_water_spectrum_set_amplitude(float a, float b) { (void)a; (void)b; }
void g3d_water_spectrum_update(float t) { (void)t; }
int  g3d_water_spectrum_bind(int a, int b) { (void)a; (void)b; return 0; }
void g3d_water_spectrum_tile_sizes(float *o) { (void)o; }
void g3d_water_spectrum_shutdown(void) {}

#endif
