/* El agua tiene que SEGUIR SIENDO AGUA.
   Con la camara baja sobre un bajio -- una plataforma poco profunda, que es lo
   que sale en casi cualquier mapa con nivel de mar -- la espuma tapaba el color
   entero: todo blanco de horizonte a horizonte y ni rastro del tono del agua.

   Esto NO mide "que se vea espuma": mide que el color del agua sobreviva. Dos
   cosas a la vez, porque una sola se enga~na facil:
     - cuantos pixeles quedan practicamente blancos (r,g,b todos altos y planos)
     - cuanto tono azul queda de media (azul menos rojo)
   Una sabana de espuma dispara el primero y aplasta el segundo. */
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
#define VW 480
#define VH 300

static float terr[S*S];
static unsigned char px[VW*VH*4];
static int fails = 0;

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

int main(void) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *w = SDL_CreateWindow("x", 0, 0, VW, VH,
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

    /* Bajio largisimo: casi todo el campo entre 0.2 y 3 de profundidad, que es
       el caso que se rompia. Al fondo sube y sale del agua. */
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float f = (float)j / (float)(S - 1);
            terr[j*S + i] = -3.0f + 5.0f * f * f;
        }

    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);

    /* Camara BAJA y casi horizontal: es la vista de la captura, y la que mas
       espuma acumula por pixel. */
    G3DCamera *cam = g3d_camera_impl_create(0);
    cam->position = vec3_make(0, 3.5f, 60);
    cam->fov = 65; cam->near_plane = 0.1f; cam->far_plane = 1200;
    cam->aspect_ratio = (float)VW / VH;
    g3d_camera_look_at_impl(cam, vec3_make(0, 1.0f, -140), vec3_make(0, 1, 0));
    g3d_camera_update(cam);
    glViewport(0, 0, VW, VH);
    glClearColor(0.45f, 0.62f, 0.85f, 1.0f);   /* cielo */

    g3d_water_render_set_surf(1.0f, 0.11f, 0.16f, 1.8f);
    g3d_water_render_set_surf_wave(0.9f, 0.0f);

    /* Peor caso en el tiempo: la espuma persistente se acumula, asi que se mide
       despues de dejarla saturar, no en el primer fotograma. */
    double worst_white = 0.0, worst_tint = 1e9;
    for (int f = 0; f < 90; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        SDL_Delay(16);
        if (f < 45 || (f % 5)) continue;

        glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);
        long white = 0, water = 0;
        double tint = 0.0;
        for (int i = 0; i < VW*VH; i++) {
            float r = px[i*4+0] / 255.0f, g = px[i*4+1] / 255.0f, b = px[i*4+2] / 255.0f;
            /* El cielo tal cual no cuenta como agua. */
            if (fabsf(r - 0.45f) < 0.02f && fabsf(g - 0.62f) < 0.02f && fabsf(b - 0.85f) < 0.02f)
                continue;
            water++;
            if (r > 0.85f && g > 0.85f && b > 0.85f) white++;
            tint += (b - r);
        }
        if (!water) continue;
        double wf = 100.0 * white / water;
        double tf = tint / water;
        if (wf > worst_white) worst_white = wf;
        if (tf < worst_tint) worst_tint = tf;
    }

    /* Y lo que SI se puede medir aqui pase lo que pase con las pasadas de
       espacio de pantalla: cuanta superficie mojada esta cubierta de espuma.
       Si casi todo el bajio pasa de 0.5, es una sabana, se vea o no en este
       arnes. */
    static float ff[S*S];
    int fside = 0;
    long wet = 0, covered = 0;
    if (g3d_water_render_foam_readback(ff, &fside)) {
        const float *dp = g3d_waterfield_depth_array();
        for (int i = 0; i < S*S; i++) {
            if (dp[i] <= 0.05f) continue;
            wet++;
            if (ff[i] > 0.5f) covered++;
        }
    }

    char buf[160];
    printf("1. la espuma no puede tapar el agua\n");
    snprintf(buf, sizeof buf, "%.0f%% de los pixeles de agua casi blancos", worst_white);
    check("no es una sabana blanca", worst_white < 25.0, buf);

    printf("2. y el tono del agua se sigue viendo\n");
    snprintf(buf, sizeof buf, "azul-rojo medio = %.3f", worst_tint);
    check("queda color de agua", worst_tint > 0.06, buf);

    printf("3. y el campo de espuma no cubre el bajio entero\n");
    double cov = wet ? 100.0 * covered / wet : 0.0;
    snprintf(buf, sizeof buf, "%.0f%% de la superficie mojada cubierta (%ld de %ld celdas)",
             cov, covered, wet);
    check("cobertura contenida", cov < 30.0, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
    return fails ? 1 : 0;
}
