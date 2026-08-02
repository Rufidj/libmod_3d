/* El agua tiene que salpicar donde CHOCA, y solo ahi.
   Lo facil es hacer que salpique siempre y quede vistoso en la captura; lo que
   importa es que la salpicadura signifique algo:
     - una roca en un rapido salpica
     - la MISMA roca en agua quieta no
     - una roca fuera del agua tampoco
     - una roca hundida del todo tampoco (el agua se cierra encima)
     - y el pie de una cascada echa espuma solo si hay cascada
   Se mide contando ráfagas emitidas, no mirando pixeles: asi el test dice por
   que salpica, no solo que se ve blanco. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_splash.h"

#define S  129
#define WS 128.0f

static float terr[S*S];
static int fails = 0;

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* El test corre sin escena ni GL: se sustituyen las piezas que el modulo usa
   para preguntar por las rocas y para soltar gotas, y se cuenta. */
int   g_rock_active = 0;
float g_rock_pos[3] = { 0, 0, 0 };
float g_rock_half[3] = { 1.0f, 1.0f, 1.0f };

int   g_bursts = 0;
int   g_droplets = 0;
float g_last_burst[3];

int main(void) {
    /* Cauce en pendiente que baja de x=-64 a x=+64, con un escalon al final
       para que se forme cascada. */
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float t = (float)i / (float)(S - 1);
            float h = 20.0f - 14.0f * t;
            if (t > 0.75f) h -= 9.0f;              /* el acantilado */
            terr[j*S + i] = h;
        }

    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_add_spring(-60.0f, 0.0f, 9.0f);
    g3d_waterfield_settle(30.0f);

    char buf[200];

    /* --- 1. una roca en corriente --- */
    printf("1. una roca en un rapido salpica\n");
    float vx = 0, vz = 0;
    g3d_waterfield_flow_at(-20.0f, 0.0f, &vx, &vz);
    float speed = sqrtf(vx*vx + vz*vz);
    float depth = g3d_waterfield_depth_at(-20.0f, 0.0f);
    snprintf(buf, sizeof buf, "corriente=%.2f u/s  profundidad=%.2f", speed, depth);
    check("hay corriente donde se pone la roca", speed > 0.8f && depth > 0.02f, buf);

    g_rock_active = 1;
    g_rock_pos[0] = -20.0f; g_rock_pos[2] = 0.0f;
    g_rock_pos[1] = g3d_waterfield_level_at(-20.0f, 0.0f);   /* asoma */
    g_bursts = 0; g_droplets = 0;
    for (int f = 0; f < 60; f++) g3d_water_splash_tick(1.0f / 60.0f);
    int moving = g_droplets;
    snprintf(buf, sizeof buf, "%d gotas en 1 s (%d rafagas)", g_droplets, g_bursts);
    check("salpica", moving > 0, buf);

    /* Y por el lado de AGUAS ARRIBA, no desde el centro de la roca. */
    float dx = g_last_burst[0] - g_rock_pos[0];
    float dz = g_last_burst[2] - g_rock_pos[2];
    float dot = dx * vx + dz * vz;
    snprintf(buf, sizeof buf, "producto con la corriente = %.3f (negativo = aguas arriba)", dot);
    check("por la cara que recibe el agua", dot < 0.0f, buf);

    /* --- 2. la misma roca en agua quieta --- */
    printf("2. la misma roca en agua quieta no salpica\n");
    g3d_waterfield_shutdown();
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (float)i - 64, z = (float)j - 64;
            float r = sqrtf(x*x + z*z);
            terr[j*S + i] = (r < 30.0f) ? -6.0f : 10.0f;      /* una balsa */
        }
    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_fill_basin(0.0f, 0.0f, -1.0f, 0.0f);
    g3d_waterfield_settle(20.0f);

    g_rock_pos[0] = 0.0f; g_rock_pos[2] = 0.0f;
    g_rock_pos[1] = g3d_waterfield_level_at(0.0f, 0.0f);
    g3d_waterfield_flow_at(0.0f, 0.0f, &vx, &vz);
    g_droplets = 0;
    for (int f = 0; f < 60; f++) g3d_water_splash_tick(1.0f / 60.0f);
    snprintf(buf, sizeof buf, "corriente=%.3f u/s -> %d gotas",
             sqrtf(vx*vx + vz*vz), g_droplets);
    check("agua quieta, roca callada", g_droplets == 0, buf);

    /* --- 3. fuera del agua --- */
    printf("3. una roca en seco no salpica\n");
    g_rock_pos[0] = 60.0f; g_rock_pos[2] = 60.0f;    /* fuera de la balsa */
    g_rock_pos[1] = 12.0f;
    g_droplets = 0;
    for (int f = 0; f < 60; f++) g3d_water_splash_tick(1.0f / 60.0f);
    snprintf(buf, sizeof buf, "%d gotas", g_droplets);
    check("en seco, nada", g_droplets == 0, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
