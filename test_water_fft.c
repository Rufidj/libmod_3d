/* Verifies the GPU spectral ocean actually produces a wave field.
 *
 * A wrong FFT rarely errors -- it silently yields zeros, NaNs, or a field that
 * does not evolve. So this reads the displacement map back and checks the
 * things that distinguish real waves from garbage: finite, non-trivial
 * variance, mean height near zero (a wave field oscillates about sea level),
 * motion over time, and a plausible crest distribution. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "libmod_3d_glcaps.h"
#include "libmod_3d_water_spectrum.h"

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

#define N 256
static int fails = 0;

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) fails++;
}

/* Pull one cascade layer of the displacement array back to the CPU. */
static float *read_layer(int layer, int *out_n) {
    int unit = 6;
    g3d_water_spectrum_bind(unit, unit + 1);
    glActiveTexture(GL_TEXTURE0 + unit);
    int n = N;
    float *all = (float *)malloc((size_t)n * n * 3 * 4 * sizeof(float));
    if (!all) return NULL;
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_FLOAT, all);
    glActiveTexture(GL_TEXTURE0);
    *out_n = n;
    /* the requested layer starts after `layer` full images */
    float *layerData = (float *)malloc((size_t)n * n * 4 * sizeof(float));
    if (!layerData) { free(all); return NULL; }
    memcpy(layerData, all + (size_t)layer * n * n * 4, (size_t)n * n * 4 * sizeof(float));
    free(all);
    return layerData;
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("no SDL\n"); return 77; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window *w = SDL_CreateWindow("fft", 0, 0, 64, 64,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!w) { printf("no window\n"); SDL_Quit(); return 77; }
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    if (!ctx) { printf("no context\n"); SDL_Quit(); return 77; }

    char buf[200];
    printf("1. init\n");
    if (!g3d_glcaps()->compute) { printf("no compute; skipping\n"); return 77; }
    check("spectrum initialises", g3d_water_spectrum_init(N), "");
    check("reports ready", g3d_water_spectrum_ready() == 1, "");

    g3d_water_spectrum_set_wind(1.0f, 0.3f, 16.0f, 1.0f);
    g3d_water_spectrum_set_amplitude(1.0f, 1.0f);

    printf("2. one update produces a wave field\n");
    g3d_water_spectrum_update(10.0f);
    glFinish();
    while (glGetError() != GL_NO_ERROR) {}

    int n = 0;
    float *d = read_layer(0, &n);
    check("displacement read back", d != NULL, "");
    if (!d) return 1;

    long bad = 0;
    double sum = 0.0, sum2 = 0.0, amax = 0.0;
    for (int i = 0; i < n * n; i++) {
        float hy = d[i * 4 + 1];
        if (!isfinite(hy) || !isfinite(d[i * 4]) || !isfinite(d[i * 4 + 2])) { bad++; continue; }
        sum += hy; sum2 += (double)hy * hy;
        if (fabs(hy) > amax) amax = fabs(hy);
    }
    check("no NaN or infinity in the field", bad == 0, "");

    double mean = sum / (n * n);
    double var = sum2 / (n * n) - mean * mean;
    double rms = sqrt(var > 0 ? var : 0);
    snprintf(buf, sizeof(buf), "rms=%.4f max=%.4f", rms, amax);
    check("field has real wave energy", rms > 1e-4 && amax > 1e-3, buf);

    /* Pin the absolute scale. The Phillips constant is arbitrary, so nothing
       else stops a tweak turning the default sea into a hurricane; at
       amplitude = 1 this should be a moderate sea, not a millpond or a storm. */
    snprintf(buf, sizeof(buf), "rms=%.3f (want 0.2 .. 1.5 at amplitude 1)", rms);
    check("default wave height is a moderate sea", rms > 0.2 && rms < 1.5, buf);

    snprintf(buf, sizeof(buf), "mean=%.6f vs rms=%.4f", mean, rms);
    check("waves oscillate about sea level", fabs(mean) < rms * 0.25, buf);

    /* A Gaussian wave field has crests a few sigma tall, not hundreds:
       a runaway FFT shows up here as an enormous crest factor. */
    double crest = amax / (rms > 1e-9 ? rms : 1.0);
    snprintf(buf, sizeof(buf), "max/rms=%.2f", crest);
    check("crest factor is physical", crest > 2.0 && crest < 12.0, buf);

    printf("3. the sea evolves over time\n");
    float *d0 = (float *)malloc((size_t)n * n * 4 * sizeof(float));
    memcpy(d0, d, (size_t)n * n * 4 * sizeof(float));
    g3d_water_spectrum_update(10.5f);
    glFinish();
    free(d);
    d = read_layer(0, &n);
    double diff = 0.0;
    for (int i = 0; i < n * n; i++) diff += fabs(d[i * 4 + 1] - d0[i * 4 + 1]);
    diff /= (n * n);
    snprintf(buf, sizeof(buf), "mean |dh| = %.5f over half a second", diff);
    check("field changes with time", diff > rms * 0.02, buf);

    printf("4. every cascade carries its own band\n");
    for (int c = 0; c < G3D_WATER_CASCADES; c++) {
        float *dc = read_layer(c, &n);
        double s2 = 0.0;
        for (int i = 0; i < n * n; i++) s2 += (double)dc[i * 4 + 1] * dc[i * 4 + 1];
        double r = sqrt(s2 / (n * n));
        snprintf(buf, sizeof(buf), "cascade %d rms=%.5f", c, r);
        check("cascade is non-empty", r > 1e-6, buf);
        free(dc);
    }

    printf("5. no GL errors across 60 updates\n");
    int errs = 0;
    for (int i = 0; i < 60; i++) {
        g3d_water_spectrum_update(20.0f + (float)i * 0.016f);
        while (glGetError() != GL_NO_ERROR) errs++;
    }
    check("60 frames clean", errs == 0, "");

    free(d); free(d0);
    g3d_water_spectrum_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
    SDL_Quit();
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
