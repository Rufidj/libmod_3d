/*
 * libmod_3d_erosion.c - Hydraulic erosion over a height field
 *
 * See the header for what this is for. The loop per step is:
 *
 *   1. rain            water arrives everywhere
 *   2. flow            virtual pipes push it downhill, same as the water sim
 *   3. erode/deposit   fast water tears ground up, slow water drops it
 *   4. transport       the sediment travels with the water
 *   5. evaporate       water leaves, and what it carried settles
 *
 * Steps 3 and 4 are the only ones the water simulation does not already do.
 * The rest is deliberately the same model, so an eroded terrain and the river
 * that later runs on it agree about where the water goes.
 */

#include "libmod_3d_erosion.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

void g3d_erosion_defaults(G3DErosionParams *p) {
    if (!p) return;
    p->rain        = 0.012f;
    p->evaporation = 0.020f;
    p->capacity    = 0.60f;
    p->dissolve    = 0.30f;
    p->deposit     = 0.30f;
    /* Without a floor the capacity goes to zero on flat ground, no channel ever
       starts, and the whole run does nothing. */
    p->min_slope   = 0.02f;
    p->talus       = 0.0f;
    p->gravity     = 9.81f;
}

int g3d_erosion_run(float *h, int side, float world_size,
                    int iterations, const G3DErosionParams *par) {
    if (!h || side < 4 || iterations <= 0) return 0;
    G3DErosionParams p;
    if (par) p = *par; else g3d_erosion_defaults(&p);

    int N = side * side;
    float cell = world_size / (float)(side - 1);
    if (cell <= 0.0f) cell = 1.0f;

    float *w   = (float *)calloc((size_t)N, sizeof(float)); /* agua           */
    float *s   = (float *)calloc((size_t)N, sizeof(float)); /* sedimento      */
    float *s2  = (float *)calloc((size_t)N, sizeof(float)); /* sedimento tras mover */
    float *fL  = (float *)calloc((size_t)N, sizeof(float));
    float *fR  = (float *)calloc((size_t)N, sizeof(float));
    float *fT  = (float *)calloc((size_t)N, sizeof(float));
    float *fB  = (float *)calloc((size_t)N, sizeof(float));
    float *vx  = (float *)calloc((size_t)N, sizeof(float));
    float *vz  = (float *)calloc((size_t)N, sizeof(float));
    float *wp  = (float *)calloc((size_t)N, sizeof(float)); /* agua antes del balance */
    if (!w || !s || !s2 || !fL || !fR || !fT || !fB || !vx || !vz) {
        free(w); free(s); free(s2); free(fL); free(fR); free(fT); free(fB);
        free(vx); free(vz); free(wp);
        return 0;
    }

    /* Paso fijo y conservador. La erosion no es tiempo real: importa que sea
       ESTABLE, porque una sola iteracion que se dispare deja picos que ya no se
       quitan. */
    const float dt = 0.06f;

    /* SUBDIVISION AUTOMATICA. Con los mandos fuertes, arrancar demasiado material
       de una vez no talla mas rapido: escalona. El terreno sale con peldanos
       duros alineados a la rejilla, que es una inestabilidad, no un barranco --
       medido, sube el numero de saltos bruscos por encima del que tenia el
       terreno original.
       Repartir la MISMA erosion en pasos mas suaves lo arregla del todo, asi que
       se hace aqui y no en la interfaz: los mandos deciden CUANTA erosion, nunca
       si el resultado es estable. */
    float aggress = p.capacity * ((p.dissolve > p.deposit) ? p.dissolve : p.deposit)
                  * (p.rain / 0.012f);
    int sub = (int)ceilf(aggress / 0.20f);
    if (sub < 1)  sub = 1;
    if (sub > 12) sub = 12;
    if (sub > 1) {
        p.rain     /= (float)sub;
        p.dissolve /= (float)sub;
        p.deposit  /= (float)sub;
        iterations *= sub;
    }
    const float area = cell * cell;
    int done = 0;

    for (int it = 0; it < iterations; it++) {
        /* --- 1. lluvia --- */
        for (int i = 0; i < N; i++) w[i] += p.rain * dt;
        memcpy(wp, w, (size_t)N * sizeof(float));

        /* --- 2. caudales por tuberias virtuales --- */
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++) {
                int c = j * side + i;
                float hc = h[c] + w[c];
                float acc = dt * p.gravity * cell;
                float dL = (i > 0)        ? hc - (h[c-1]    + w[c-1])    : 0.0f;
                float dR = (i < side - 1) ? hc - (h[c+1]    + w[c+1])    : 0.0f;
                float dT = (j > 0)        ? hc - (h[c-side] + w[c-side]) : 0.0f;
                float dB = (j < side - 1) ? hc - (h[c+side] + w[c+side]) : 0.0f;
                float nL = fL[c] + acc * dL; if (nL < 0.0f) nL = 0.0f;
                float nR = fR[c] + acc * dR; if (nR < 0.0f) nR = 0.0f;
                float nT = fT[c] + acc * dT; if (nT < 0.0f) nT = 0.0f;
                float nB = fB[c] + acc * dB; if (nB < 0.0f) nB = 0.0f;
                /* Una celda no puede soltar mas agua de la que tiene: sin este
                   tope el nivel se hace negativo y la simulacion se rompe. */
                float out = (nL + nR + nT + nB) * dt;
                float have = w[c] * area;
                if (out > have && out > 1e-9f) {
                    float k = have / out;
                    nL *= k; nR *= k; nT *= k; nB *= k;
                }
                fL[c] = nL; fR[c] = nR; fT[c] = nT; fB[c] = nB;
            }

        /* --- balance de agua y velocidad --- */
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++) {
                int c = j * side + i;
                float in = 0.0f;
                if (i > 0)        in += fR[c-1];
                if (i < side - 1) in += fL[c+1];
                if (j > 0)        in += fB[c-side];
                if (j < side - 1) in += fT[c+side];
                float out = fL[c] + fR[c] + fT[c] + fB[c];
                float w0 = w[c];
                w[c] += (in - out) * dt / area;
                if (w[c] < 0.0f) w[c] = 0.0f;
                /* La velocidad se mide sobre la profundidad MEDIA del paso: con
                   la final, una celda que casi se vacia da velocidades enormes y
                   se come el terreno de golpe. */
                float wm = 0.5f * (w0 + w[c]);
                if (wm < 1e-5f) { vx[c] = vz[c] = 0.0f; continue; }
                /* La velocidad sale de caudal/(ancho*calado). Con una lamina de
                   lluvia de milesimas, ese calado tiende a cero y la velocidad se
                   dispara: el terreno entero se erosiona por igual y no se forma
                   ni un barranco. El suelo de calado es lo que hace que la
                   velocidad ALTA aparezca solo donde el agua se junta de verdad. */
                float wd = (wm < 0.02f) ? 0.02f : wm;
                float ux = 0.0f, uz = 0.0f;
                if (i > 0)        ux += fR[c-1];
                if (i < side - 1) ux -= fL[c+1];
                ux += fR[c] - fL[c];
                if (j > 0)        uz += fB[c-side];
                if (j < side - 1) uz -= fT[c+side];
                uz += fB[c] - fT[c];
                vx[c] = 0.5f * ux / (cell * wd);
                vz[c] = 0.5f * uz / (cell * wd);
            }

        /* --- 3. arrancar y depositar --- */
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++) {
                int c = j * side + i;
                /* Pendiente local por diferencias centradas. */
                int il = (i > 0) ? c - 1 : c, ir = (i < side - 1) ? c + 1 : c;
                int jt = (j > 0) ? c - side : c, jb = (j < side - 1) ? c + side : c;
                float gx = (h[ir] - h[il]) / (cell * ((ir != il) ? 2.0f : 1.0f));
                float gz = (h[jb] - h[jt]) / (cell * ((jb != jt) ? 2.0f : 1.0f));
                float slope = sqrtf(gx*gx + gz*gz);
                float sn = slope / sqrtf(1.0f + slope*slope);   /* seno */
                if (sn < p.min_slope) sn = p.min_slope;

                float speed = sqrtf(vx[c]*vx[c] + vz[c]*vz[c]);
                float depth = w[c];
                float cap = p.capacity * sn * speed * (depth > 1.0f ? 1.0f : depth);

                if (s[c] < cap) {
                    /* Agua con hambre: arranca suelo y se lo lleva. */
                    float take = p.dissolve * (cap - s[c]) * dt;
                    /* Nunca mas de lo que hay de desnivel alrededor, o abre un
                       pozo vertical de una celda en vez de un barranco. */
                    float lim = 0.25f * cell * sn;
                    if (take > lim) take = lim;
                    h[c] -= take;
                    s[c] += take;
                } else {
                    /* Agua cargada y frenando: suelta lo que le sobra. Esto es lo
                       que forma los abanicos al pie de las laderas. */
                    float give = p.deposit * (s[c] - cap) * dt;
                    h[c] += give;
                    s[c] -= give;
                }
            }

        /* --- 4. el sedimento viaja con el agua --- */
        /* Se reparte por las MISMAS tuberias que el agua, en proporcion a lo que
           sale por cada una. Antes esto era un muestreo hacia atras, que es lo
           habitual para un fluido en pantalla pero NO conserva masa: el terreno
           perdia volumen en cada pasada y el resultado era una erosion uniforme
           en vez de barrancos. Repartiendo por caudal, lo que sale de una celda
           es exactamente lo que entra en sus vecinas. */
        memcpy(s2, s, (size_t)N * sizeof(float));
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++) {
                int c = j * side + i;
                float tot = (fL[c] + fR[c] + fT[c] + fB[c]) * dt;
                if (tot <= 1e-12f || s[c] <= 0.0f) continue;
                float have = wp[c] * area;
                float frac = (have > 1e-9f) ? (tot / have) : 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                float move = s[c] * frac;
                s2[c] -= move;
                if (i > 0)        s2[c-1]    += move * (fL[c] * dt) / tot;
                if (i < side - 1) s2[c+1]    += move * (fR[c] * dt) / tot;
                if (j > 0)        s2[c-side] += move * (fT[c] * dt) / tot;
                if (j < side - 1) s2[c+side] += move * (fB[c] * dt) / tot;
            }
        memcpy(s, s2, (size_t)N * sizeof(float));

        /* --- 5. evaporacion: lo que se va deja su carga --- */
        for (int i = 0; i < N; i++) {
            w[i] *= (1.0f - p.evaporation);
            if (w[i] < 1e-6f) {
                /* Charco seco: todo lo que llevaba se queda aqui. Si no, el
                   material desapareceria del mapa y el terreno perderia volumen
                   en cada pasada. */
                h[i] += s[i];
                s[i] = 0.0f;
                w[i] = 0.0f;
            }
        }

        if (p.talus > 0.0f) g3d_erosion_thermal(h, side, world_size, 1, p.talus);
        done++;
    }

    /* Lo que quede en suspension al terminar vuelve al suelo: la erosion MUEVE
       material, no lo hace desaparecer. */
    for (int i = 0; i < N; i++) h[i] += s[i];

    free(w); free(s); free(s2); free(fL); free(fR); free(fT); free(fB);
    free(vx); free(vz); free(wp);
    return done;
}

int g3d_erosion_thermal(float *h, int side, float world_size,
                        int iterations, float talus) {
    if (!h || side < 4 || iterations <= 0 || talus <= 0.0f) return 0;
    int N = side * side;
    float cell = world_size / (float)(side - 1);
    if (cell <= 0.0f) cell = 1.0f;
    float *d = (float *)calloc((size_t)N, sizeof(float));
    if (!d) return 0;

    int done = 0;
    for (int it = 0; it < iterations; it++) {
        memset(d, 0, (size_t)N * sizeof(float));
        for (int j = 0; j < side; j++)
            for (int i = 0; i < side; i++) {
                int c = j * side + i;
                /* Cuanto sobra respecto al vecino mas bajo, y a cuantos vecinos
                   hay que repartirlo. */
                float total = 0.0f;
                float diff[4] = { 0, 0, 0, 0 };
                int   nb[4]   = { 0, 0, 0, 0 };
                int   n = 0;
                const int off[4] = { -1, 1, -side, side };
                const int okx[4] = { i > 0, i < side - 1, j > 0, j < side - 1 };
                for (int k = 0; k < 4; k++) {
                    if (!okx[k]) continue;
                    float dh = h[c] - h[c + off[k]];
                    if (dh > talus * cell) { diff[n] = dh; nb[n] = c + off[k]; total += dh; n++; }
                }
                if (!n || total <= 0.0f) continue;
                /* Solo se mueve el EXCESO sobre el angulo estable, y la mitad,
                   para que no oscile de un lado a otro entre iteraciones. */
                float move = 0.5f * (diff[0] - talus * cell);
                if (move <= 0.0f) continue;
                for (int k = 0; k < n; k++) {
                    float part = move * (diff[k] / total);
                    d[c]     -= part;
                    d[nb[k]] += part;
                }
            }
        for (int i = 0; i < N; i++) h[i] += d[i];
        done++;
    }
    free(d);
    return done;
}
