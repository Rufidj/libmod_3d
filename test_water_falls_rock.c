/* Una cascada tiene que PARTIRSE al encontrarse una roca, y volver a juntarse
   debajo. Que salpique no basta: la espuma se ve pero la cortina sigue pasando
   por dentro de la piedra, que es justo lo que se queria arreglar.

   Lo que se mide es la geometria, comparando la MISMA cascada con y sin roca:
     - con la roca delante la cortina se parte en tiras (mas quads)
     - queda hueco: ninguna tira ocupa el tramo que tapa la piedra
     - y donde golpea aparece un punto de salpicadura
   Y lo mas importante: quitando la roca vuelve EXACTAMENTE la cortina de antes.
   Si no, cada roca dejaria geometria de mas para siempre. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include "libmod_3d_camera.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_falls.h"

#define S  129
#define WS 128.0f
/* Las tiras en que se parte una cortina (WF_SPLIT_COLS del modulo). */
#define WF_COLS_TEST 8

static float terr[S*S];
static int fails = 0;

/* El modulo de cascadas llama al de particulas para la niebla del pie. */
void g3d_particles_burst(float x, float y, float z, int count, float speed,
                         float size, float life, float r, float g, float b) {
    (void)x; (void)y; (void)z; (void)count; (void)speed;
    (void)size; (void)life; (void)r; (void)g; (void)b;
}

/* Lo que el modulo pide para PINTAR. Aqui no se pinta -- se mide la geometria --
   asi que basta con que existan: enlazar el render entero arrastraria medio
   motor a una prueba que no lo necesita. */
Mat4 g3d_camera_get_view(G3DCamera *c) { (void)c; return mat4_identity(); }
Mat4 g3d_camera_get_projection(G3DCamera *c) { (void)c; return mat4_identity(); }
void g3d_renderer_get_fog(float *a, float *b2, float *c) { (void)a; (void)b2; (void)c; }
float g3d_sky_get_ambient(void) { return 0.4f; }
void g3d_sky_get_sun(float *d, float *c) { (void)d; (void)c; }

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* La geometria se rehace bajo demanda. Se fuerza pidiendo los pies, que es la
   misma reconstruccion que hace el dibujado pero sin necesitar camara ni
   shaders: aqui se mide la geometria, no como se pinta. */
static float g_feet[256 * 5];
static int   g_nfeet = 0;
static void rebuild(void) { g_nfeet = g3d_water_falls_feet(g_feet, 256); }

int main(void) {
    char b[220];
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window *w = SDL_CreateWindow("r", 0, 0, 64, 64,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = SDL_GL_CreateContext(w);

    /* Meseta con un cauce en V que lleva el agua hasta el borde del acantilado,
       en x = 0, donde cae en seco. */
    const float TOP = 20.0f, BOT = 4.0f;
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float dz = fabsf((float)j - (float)(S/2));
            float groove = (dz < 6.0f) ? (2.5f * (1.0f - dz / 6.0f)) : 0.0f;
            terr[j*S + i] = (i < S/2)
                          ? TOP + 4.0f * (1.0f - (float)i / (float)(S/2)) - groove
                          : BOT;
        }

    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_add_spring(-56.0f, 0.0f, 14.0f);
    g3d_waterfield_settle(60.0f);

    g3d_water_falls_set_obstacles(NULL, 0);
    rebuild();
    int limpio = g3d_water_falls_count();
    snprintf(b, sizeof b, "%d cortinas", limpio);
    check("hay cascada de la que hablar", limpio > 0, b);
    snprintf(b, sizeof b, "%d partidas", g3d_water_falls_split_count());
    check("sin rocas no se parte nada", g3d_water_falls_split_count() == 0, b);

    printf("1. con una roca delante, la cortina se abre\n");
    /* Una piedra a media pared, justo en el cauce (x ~ 0, z ~ 0). */
    const float ROCK_Y0 = 9.0f, ROCK_Y1 = 14.0f;
    float rock[6] = { -3.0f, ROCK_Y0, -4.0f, 3.0f, ROCK_Y1, 4.0f };
    g3d_water_falls_set_obstacles(rock, 1);
    rebuild();
    int conroca = g3d_water_falls_count();
    snprintf(b, sizeof b, "%d cortinas con roca, %d sin ella", conroca, limpio);
    check("la cortina se parte en tiras", conroca > limpio, b);
    snprintf(b, sizeof b, "%d cortinas partidas", g3d_water_falls_split_count());
    check("y se registra como partida", g3d_water_falls_split_count() > 0, b);

    printf("2. y queda HUECO donde esta la piedra\n");
    /* El hueco se comprueba CONTANDO. La roca es mas ancha que la cortina y su
       tramo vertical cae entero dentro del salto, asi que toda tira tiene que
       salir en exactamente DOS trozos: el de encima y el de debajo. Si alguna
       saliera de una pieza, esa seguiria atravesando la piedra, y la cuenta no
       daria. */
    int esperado = limpio * WF_COLS_TEST * 2;
    snprintf(b, sizeof b, "%d trozos, esperados %d (%d cortinas x %d tiras x 2)",
             conroca, esperado, limpio, WF_COLS_TEST);
    check("cada tira sale en dos: encima y debajo, nada en medio",
          conroca == esperado, b);

    /* Y ademas salpica donde golpea, no en el suelo. */
    const float *feet = g_feet;
    int nf = g_nfeet;
    int golpe = 0; float mejor = -1e9f;
    for (int i = 0; i < nf; i++) {
        float fx = feet[i*5], fy = feet[i*5+1], fz = feet[i*5+2];
        if (fx < -3.5f || fx > 3.5f || fz < -4.5f || fz > 4.5f) continue;
        if (fabsf(fy - ROCK_Y1) < 0.5f) { golpe++; if (fy > mejor) mejor = fy; }
    }
    snprintf(b, sizeof b, "%d puntos de impacto en la cara de la roca (y=%.1f, roca hasta %.1f)",
             golpe, mejor, ROCK_Y1);
    check("salpica DONDE golpea, no en el suelo", golpe > 0, b);

    printf("3. al quitar la roca vuelve la cascada de antes\n");
    g3d_water_falls_set_obstacles(NULL, 0);
    rebuild();
    int vuelta = g3d_water_falls_count();
    snprintf(b, sizeof b, "%d cortinas, antes %d", vuelta, limpio);
    check("exactamente la misma geometria", vuelta == limpio, b);
    snprintf(b, sizeof b, "%d partidas", g3d_water_falls_split_count());
    check("y sin marcas de partido", g3d_water_falls_split_count() == 0, b);

    printf("4. una roca fuera del salto no toca nada\n");
    float lejos[6] = { 40.0f, 0.0f, 40.0f, 46.0f, 6.0f, 46.0f };
    g3d_water_falls_set_obstacles(lejos, 1);
    rebuild();
    snprintf(b, sizeof b, "%d cortinas, %d partidas",
             g3d_water_falls_count(), g3d_water_falls_split_count());
    check("la cascada sigue entera",
          g3d_water_falls_count() == limpio && g3d_water_falls_split_count() == 0, b);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    g3d_water_falls_shutdown();
    g3d_waterfield_shutdown();
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(w); SDL_Quit();
    return fails ? 1 : 0;
}
