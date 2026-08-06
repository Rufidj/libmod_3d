/* La erosion tiene que TALLAR, no suavizar.
   Un promediado tambien cambia el terreno y encima queda "suave", asi que es
   facil colar un suavizado por erosion. Lo que la distingue:
     - baja donde el agua corre y SUBE donde se remansa (deposito)
     - el material se mueve, no desaparece: el volumen se conserva
     - las laderas se surcan: aparece estructura fina que antes no estaba
     - y no explota: nada de NaN ni picos disparados
   Se compara siempre contra el MISMO terreno de partida. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_erosion.h"

#define S  129
#define WS 400.0f

static float base[S*S], work[S*S], smooth[S*S];
static int fails = 0;

static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* Un cono con ruido suave: una montana con laderas largas, que es donde la
   erosion tiene algo que hacer. */
static void build(void) {
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (float)i - S/2, z = (float)j - S/2;
            float r = sqrtf(x*x + z*z) / (S * 0.5f);
            float h = 60.0f * (1.0f - r);
            if (h < 0.0f) h = 0.0f;
            h += 4.0f * sinf(i * 0.21f) * cosf(j * 0.17f)
               + 2.0f * sinf(i * 0.07f + 1.3f) * sinf(j * 0.11f);
            base[j*S + i] = h;
        }
}

static double volume(const float *h) {
    double v = 0; for (int i = 0; i < S*S; i++) v += h[i]; return v;
}

/* "Rugosidad" a escala pequena: cuanto se aparta cada celda de la media de sus
   vecinos. Un barranco la sube; un suavizado la baja. Es la medida que separa
   erosionar de promediar. */
static double detail(const float *h) {
    double sum = 0; int n = 0;
    for (int j = 1; j < S-1; j++)
        for (int i = 1; i < S-1; i++) {
            int c = j*S + i;
            double avg = 0.25 * (h[c-1] + h[c+1] + h[c-S] + h[c+S]);
            sum += fabs(h[c] - avg); n++;
        }
    return n ? sum / n : 0.0;
}

int main(void) {
    char buf[220];
    build();
    for (int i = 0; i < S*S; i++) work[i] = base[i];

    G3DErosionParams p;
    g3d_erosion_defaults(&p);
    int steps = g3d_erosion_run(work, S, WS, 120, &p);
    snprintf(buf, sizeof buf, "%d iteraciones", steps);
    check("la erosion corre", steps == 120, buf);

    printf("1. nada se rompe\n");
    int bad = 0; float mx = -1e9f, mn = 1e9f;
    for (int i = 0; i < S*S; i++) {
        if (!isfinite(work[i])) bad++;
        if (work[i] > mx) mx = work[i];
        if (work[i] < mn) mn = work[i];
    }
    snprintf(buf, sizeof buf, "%d valores rotos, rango %.1f..%.1f (antes %.1f..%.1f)",
             bad, mn, mx, 0.0f, 64.0f);
    check("sin NaN ni picos disparados", bad == 0 && mx < 200.0f && mn > -200.0f, buf);

    printf("2. el material se mueve, no desaparece\n");
    double v0 = volume(base), v1 = volume(work);
    snprintf(buf, sizeof buf, "volumen %.0f -> %.0f (%.2f%% de diferencia)",
             v0, v1, 100.0 * fabs(v1 - v0) / v0);
    check("el volumen se conserva", fabs(v1 - v0) < v0 * 0.02, buf);

    printf("3. talla, no promedia\n");
    /* El mismo terreno, suavizado hasta cambiar tanto como la erosion: si la
       erosion solo suavizara, las dos medidas irian igual. */
    for (int i = 0; i < S*S; i++) smooth[i] = base[i];
    for (int it = 0; it < 6; it++) {
        static float t[S*S];
        for (int j = 1; j < S-1; j++)
            for (int i = 1; i < S-1; i++) {
                int c = j*S + i;
                t[c] = 0.2f * (smooth[c] + smooth[c-1] + smooth[c+1] + smooth[c-S] + smooth[c+S]);
            }
        for (int j = 1; j < S-1; j++)
            for (int i = 1; i < S-1; i++) smooth[j*S+i] = t[j*S+i];
    }
    double d0 = detail(base), de = detail(work), ds = detail(smooth);
    snprintf(buf, sizeof buf, "detalle: original=%.4f  erosionado=%.4f  suavizado=%.4f",
             d0, de, ds);
    /* El discriminante bueno no es "mas que el suavizado" sino el SIGNO: tallar
       anade estructura fina (sube sobre el original) y promediar la quita. */
    check("la erosion anade detalle", de > d0, buf);
    check("y un suavizado lo quita (control)", ds < d0, buf);

    printf("4. baja arriba y deposita abajo\n");
    /* Cinturon alto (ladera) contra el llano de fuera. */
    double up_d = 0, low_d = 0; int nu = 0, nl = 0;
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (float)i - S/2, z = (float)j - S/2;
            float r = sqrtf(x*x + z*z) / (S * 0.5f);
            float d = work[j*S+i] - base[j*S+i];
            if (r > 0.15f && r < 0.5f) { up_d += d; nu++; }      /* ladera */
            else if (r > 0.85f)        { low_d += d; nl++; }     /* al pie */
        }
    up_d /= (nu ? nu : 1); low_d /= (nl ? nl : 1);
    snprintf(buf, sizeof buf, "ladera %+.3f   pie %+.3f", up_d, low_d);
    check("la ladera pierde y el pie gana", up_d < 0.0 && low_d > 0.0, buf);

    printf("5. la erosion termica quita los picos imposibles\n");
    for (int i = 0; i < S*S; i++) work[i] = base[i];
    work[(S/2)*S + S/2] += 80.0f;             /* una aguja absurda */
    g3d_erosion_thermal(work, S, WS, 60, 0.35f);
    float spike = work[(S/2)*S + S/2] - base[(S/2)*S + S/2];
    snprintf(buf, sizeof buf, "aguja de +80.0 -> +%.1f", spike);
    check("la aguja se derrumba", spike < 30.0f, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
