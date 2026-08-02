/* Una cascada tiene que echar espuma DONDE CAE.
   La niebla del pie no se coloca a mano: sale del mismo sitio donde el modulo de
   cascadas cuelga la cortina, para que no pueda separarse de ella cuando el rio
   se mueva. Aqui se comprueba justo eso -- que el pie esta al fondo del salto y
   no arriba, y que solo hay gotas si de verdad hay cascada. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_falls.h"
#include "libmod_3d_water_splash.h"

#define S  129
#define WS 128.0f

static float terr[S*S];
static int fails = 0;

/* Se cuentan las gotas sustituyendo la salida del sistema de particulas. */
int   g_bursts = 0, g_droplets = 0;
float g_low = 1e9f, g_high = -1e9f;
void g3d_particles_burst(float x, float y, float z, int count, float speed,
                         float size, float life, float r, float g, float b) {
    (void)x; (void)z; (void)speed; (void)size; (void)life; (void)r; (void)g; (void)b;
    g_bursts++; g_droplets += count;
    if (y < g_low)  g_low  = y;
    if (y > g_high) g_high = y;
}

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

int main(void) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window *w = SDL_CreateWindow("f", 0, 0, 64, 64,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = SDL_GL_CreateContext(w);

    /* Meseta con pendiente y un cauce en V que lleva el agua hasta el borde,
       donde cae en seco: un acantilado limpio. Sin cauce el manantial se
       desparrama por la meseta y no llega nunca al salto. */
    const float TOP = 20.0f, BOT = 4.0f;
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float dz = fabsf((float)j - (float)(S/2));
            float groove = (dz < 6.0f) ? (2.5f * (1.0f - dz / 6.0f)) : 0.0f;
            float h = (i < S/2)
                    ? TOP + 4.0f * (1.0f - (float)i / (float)(S/2)) - groove
                    : BOT;
            terr[j*S + i] = h;
        }

    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_add_spring(-50.0f, 0.0f, 12.0f);
    g3d_waterfield_settle(40.0f);

    char buf[200];
    float feet[64 * 5];
    int n = g3d_water_falls_feet(feet, 64);
    snprintf(buf, sizeof buf, "%d pies, %d cortinas", n, g3d_water_falls_count());
    check("el salto produce cascada", n > 0, buf);

    if (n > 0) {
        float lowest = 1e9f, highest = -1e9f;
        for (int i = 0; i < n; i++) {
            if (feet[i*5+1] < lowest)  lowest  = feet[i*5+1];
            if (feet[i*5+1] > highest) highest = feet[i*5+1];
        }
        snprintf(buf, sizeof buf, "pie mas alto=%.2f, borde=%.2f", highest, TOP);
        check("el pie esta ABAJO, no en el borde", highest < TOP - 2.0f, buf);

        g_bursts = 0; g_droplets = 0; g_low = 1e9f; g_high = -1e9f;
        for (int f = 0; f < 60; f++) g3d_water_splash_tick(1.0f / 60.0f);
        snprintf(buf, sizeof buf, "%d gotas en 1 s (%d rafagas)", g_droplets, g_bursts);
        check("hay niebla en el pie", g_droplets > 0, buf);

        snprintf(buf, sizeof buf, "gotas entre y=%.2f y y=%.2f", g_low, g_high);
        check("y sale del fondo del salto", g_high < TOP - 2.0f, buf);
    }

    /* Sin agua no hay cascada, y por tanto tampoco niebla. */
    printf("y sin rio, ni cascada ni niebla\n");
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_settle(2.0f);
    int dry = g3d_water_falls_feet(feet, 64);
    g_droplets = 0;
    for (int f = 0; f < 30; f++) g3d_water_splash_tick(1.0f / 60.0f);
    snprintf(buf, sizeof buf, "%d pies, %d gotas", dry, g_droplets);
    check("acantilado seco, nada", dry == 0 && g_droplets == 0, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
    return fails ? 1 : 0;
}
