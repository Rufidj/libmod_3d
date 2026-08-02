/* El agua tiene que RODEAR las rocas, no atravesarlas.
   Salpicar en la roca sin desviarse es un truco: se ve la espuma pero el rio
   sigue pasando por dentro de la piedra. Lo que se mide aqui es la desviacion
   de verdad, con las tres cosas que la delatan:
     - dentro de la roca no queda agua corriendo
     - aguas arriba se amontona (choca contra algo)
     - y por los lados pasa mas que antes (se va por donde puede)
   Se compara el MISMO cauce con y sin roca, asi que no hay forma de que pase
   por casualidad. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_water_field.h"

#define S  129
#define WS 128.0f

static float terr[S*S];
static int fails = 0;

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* Cauce en pendiente de -x a +x, con un surco en z para que el agua vaya
   concentrada y se note lo que hace al encontrarse la piedra. */
static void build_river(void) {
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float t = (float)i / (float)(S - 1);
            float dz = fabsf((float)j - (float)(S/2));
            float groove = (dz < 10.0f) ? (3.0f * (1.0f - dz / 10.0f)) : 0.0f;
            terr[j*S + i] = 24.0f - 16.0f * t - groove;
        }
}

/* Agua en la banda de AL LADO de la roca. Dos puntos sueltos no valen: el
   cauce es estrecho y el agua desviada se reparte, asi que lo que cuenta es
   cuanta hay en toda la franja lateral. */
static float side_water(void) {
    double sum = 0.0;
    for (float x = -6.0f; x <= 6.0f; x += 1.0f)
        for (float z = 4.0f; z <= 12.0f; z += 1.0f) {
            sum += g3d_waterfield_depth_at(x,  z);
            sum += g3d_waterfield_depth_at(x, -z);
        }
    return (float)sum;
}

static void run(float *out_depth, int n_samples, const float *rock) {
    g3d_waterfield_shutdown();
    build_river();
    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_add_spring(-56.0f, 0.0f, 14.0f);
    if (rock) g3d_waterfield_set_obstacles(rock, 1);
    g3d_waterfield_settle(60.0f);
    (void)n_samples; (void)out_depth;
}

int main(void) {
    char buf[220];

    /* La roca: un bloque de 8x8 en mitad del cauce, en x=0. */
    const float rock[4] = { -4.0f, -4.0f, 4.0f, 4.0f };

    /* --- sin roca --- */
    run(NULL, 0, NULL);
    float free_mid    = g3d_waterfield_depth_at(0.0f, 0.0f);
    float free_up     = g3d_waterfield_depth_at(-8.0f, 0.0f);
    float free_side   = side_water();
    printf("cauce libre: centro=%.3f  arriba=%.3f  lados=%.3f\n",
           free_mid, free_up, free_side);
    check("el rio corre por el centro", free_mid > 0.01f, NULL);

    /* --- con roca --- */
    run(NULL, 0, rock);
    float rock_mid  = g3d_waterfield_depth_at(0.0f, 0.0f);
    float rock_up   = g3d_waterfield_depth_at(-8.0f, 0.0f);
    float rock_side = side_water();
    printf("con roca:    centro=%.3f  arriba=%.3f  lados=%.3f\n",
           rock_mid, rock_up, rock_side);

    printf("1. el agua no atraviesa la piedra\n");
    snprintf(buf, sizeof buf, "%.4f dentro de la roca (antes %.4f)", rock_mid, free_mid);
    check("dentro no corre agua", rock_mid < free_mid * 0.35f, buf);
    check("la celda queda marcada", g3d_waterfield_obstacle_at(0.0f, 0.0f) == 1, NULL);
    check("y justo al lado no", g3d_waterfield_obstacle_at(0.0f, 7.0f) == 0, NULL);

    printf("2. se amontona aguas arriba\n");
    snprintf(buf, sizeof buf, "%.4f -> %.4f justo delante de la roca", free_up, rock_up);
    check("sube el nivel al chocar", rock_up > free_up * 1.05f, buf);

    printf("3. y se va por los lados\n");
    snprintf(buf, sizeof buf, "%.3f -> %.3f de agua en la franja lateral", free_side, rock_side);
    check("pasa mas agua por fuera", rock_side > free_side * 1.05f, buf);

    printf("4. quitar la roca lo deja como estaba\n");
    g3d_waterfield_set_obstacles(NULL, 0);
    check("se libera la celda", g3d_waterfield_obstacle_at(0.0f, 0.0f) == 0, NULL);
    g3d_waterfield_settle(60.0f);
    float back = g3d_waterfield_depth_at(0.0f, 0.0f);
    snprintf(buf, sizeof buf, "%.4f (libre era %.4f)", back, free_mid);
    check("el rio vuelve al centro", back > free_mid * 0.5f, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
