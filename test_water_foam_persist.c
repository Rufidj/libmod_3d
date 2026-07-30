/* La espuma tiene que RECORDAR.
   La espuma que solo es funcion de la fase de la ola aparece y desaparece con
   ella, y eso es medio motivo de que el agua de shader se note shader. La de
   verdad se queda: la ola rompe, deja la balsa blanca flotando, la corriente la
   estira y tarda segundos en deshacerse.

   Aqui se mide justo eso, no que "se vea espuma": se genera durante un rato, se
   corta la generacion en seco, y se comprueba que lo que queda baja POCO A POCO.
   Con la espuma vieja (instantanea) este test daria 0 en el primer fotograma
   despues del corte. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_camera.h"

#define S  161
#define WS 400.0f
#define VW 320
#define VH 200

static float terr[S*S];
static float foam[S*S];
static int fails = 0;

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* Solo cuentan las celdas con fondo hondo: en el bajio hay un suelo de espuma
   constante (el agua siempre esta revuelta ahi) que no prueba nada sobre la
   memoria. */
static float foam_mass_deep(void) {
    const float *dep = g3d_waterfield_depth_array();
    double sum = 0.0;
    for (int i = 0; i < S*S; i++)
        if (dep[i] > 1.2f) sum += foam[i];
    return (float)sum;
}

static float foam_peak_deep(void) {
    const float *dep = g3d_waterfield_depth_array();
    float m = 0.0f;
    for (int i = 0; i < S*S; i++)
        if (dep[i] > 1.2f && foam[i] > m) m = foam[i];
    return m;
}

int main(void) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *w = SDL_CreateWindow("f", 0, 0, VW, VH,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    GLuint fbo, col, dep;
    glGenTextures(1, &col); glBindTexture(GL_TEXTURE_2D, col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VW, VH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &dep); glBindRenderbuffer(GL_RENDERBUFFER, dep);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, VW, VH);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, dep);

    /* Playa: rampa suave de hondo a seco, que es donde rompen las olas. */
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++)
            terr[j*S + i] = -9.0f + 14.0f * ((float)j / (float)(S - 1));

    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);

    G3DCamera *cam = g3d_camera_impl_create(0);
    cam->position = vec3_make(0, 40, 150);
    cam->fov = 60; cam->near_plane = 0.1f; cam->far_plane = 900;
    cam->aspect_ratio = (float)VW / VH;
    g3d_camera_look_at_impl(cam, vec3_make(0, 0, 0), vec3_make(0, 1, 0));
    g3d_camera_update(cam);
    glViewport(0, 0, VW, VH);

    g3d_water_render_set_surf(1.0f, 0.11f, 0.16f, 1.8f);
    g3d_water_render_set_surf_wave(1.2f, 0.0f);

    int side = 0;
    if (!g3d_water_render_foam_readback(foam, &side)) {
        /* primer fotograma: hay que dibujar una vez para que exista el campo */
    }

    /* --- 1. se genera --------------------------------------------------- */
    printf("1. la rompiente genera espuma\n");
    /* Tiempo REAL: el pase usa el reloj, asi que hay que dejarlo correr. */
    for (int f = 0; f < 60; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        SDL_Delay(16);
    }
    if (!g3d_water_render_foam_readback(foam, &side)) {
        printf("  [SKIP] sin compute: la espuma persistente no aplica en este equipo\n");
        return 0;
    }
    float peak = foam_peak_deep();
    float m0   = foam_mass_deep();
    char buf[160];
    snprintf(buf, sizeof buf, "pico=%.2f masa=%.0f", peak, m0);
    check("aparece espuma en agua honda", peak > 0.3f && m0 > 20.0f, buf);

    /* --- 2. recuerda ---------------------------------------------------- */
    /* Se corta la generacion en seco. Lo que quede en el campo a partir de
       aqui es memoria pura. */
    printf("2. al cortar la generacion NO desaparece de golpe\n");
    g3d_water_render_set_surf(0.0f, 0.11f, 0.16f, 1.8f);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    SDL_Delay(16);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    g3d_water_render_foam_readback(foam, &side);
    float m1 = foam_mass_deep();
    snprintf(buf, sizeof buf, "%.0f -> %.0f un fotograma despues (%.0f%%)",
             m0, m1, 100.0 * m1 / (m0 > 0 ? m0 : 1));
    check("sobrevive al fotograma siguiente", m1 > m0 * 0.85f, buf);

    /* --- 3. y se deshace poco a poco ------------------------------------ */
    printf("3. y se deshace en segundos, no en fotogramas\n");
    for (int f = 0; f < 60; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        SDL_Delay(16);
    }
    g3d_water_render_foam_readback(foam, &side);
    float m2 = foam_mass_deep();
    float frac = m2 / (m0 > 0 ? m0 : 1);
    snprintf(buf, sizeof buf, "queda el %.0f%% tras ~1 s", 100.0 * frac);
    /* Con decaimiento 0.22/s, un segundo deja ~0.80. Ni congelada ni borrada. */
    check("baja de forma gradual", frac > 0.55f && frac < 0.97f, buf);

    for (int f = 0; f < 180; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        SDL_Delay(16);
    }
    g3d_water_render_foam_readback(foam, &side);
    float m3 = foam_mass_deep();
    snprintf(buf, sizeof buf, "queda el %.0f%% tras ~4 s", 100.0 * m3 / (m0 > 0 ? m0 : 1));
    check("acaba deshaciendose", m3 < m2 * 0.85f, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
    return fails ? 1 : 0;
}
