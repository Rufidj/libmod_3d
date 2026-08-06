/* Lo sembrado tiene que sobrevivir a guardar y cargar, EXACTO.
   Es donde de verdad se pierde el trabajo: sembrar mil arboles y que al reabrir
   la escena aparezcan novecientos, o desplazados medio metro, o todos del mismo
   tamano. Aqui se comprueba posicion a posicion, no por conteo -- un conteo
   correcto con las posiciones mal es exactamente el fallo que no se ve hasta que
   ya has perdido el trabajo.

   El construir grupos de instancias necesita GL y modelos de verdad, asi que eso
   no se prueba aqui: esto mide los datos, que es lo que se guarda. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "libmod_3d_scatter.h"
extern void g3d_scatter_set_kind_solid(const char*,int);
extern int  g3d_scatter_get_kind_solid(int);

#define PATH "/tmp/_scatter_test.bin"

static int fails = 0;
static void check(const char *w, int ok, const char *d) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", w, d && *d ? " -- " : "", d ? d : "");
    if (!ok) fails++;
}

/* Siembra reproducible: los mismos valores antes y despues. */
static void place(int n) {
    for (int i = 0; i < n; i++) {
        float t = (float)i;
        const char *asset = (i % 3 == 0) ? "Assets/tree.glb"
                          : (i % 3 == 1) ? "Assets/rock.glb"
                                         : "Assets/palm.glb";
        g3d_scatter_add(asset,
                        sinf(t * 0.7f) * 180.0f,
                        cosf(t * 0.3f) * 12.0f,
                        cosf(t * 1.1f) * 180.0f,
                        fmodf(t * 37.0f, 360.0f),
                        0.6f + 0.9f * fabsf(sinf(t * 0.13f)));
    }
}

int main(void) {
    char buf[220];
    const int N = 1500;

    g3d_scatter_clear();
    place(N);
    snprintf(buf, sizeof buf, "%d plantados, %d especies",
             g3d_scatter_count(), g3d_scatter_kinds());
    check("se registra la siembra", g3d_scatter_count() == N && g3d_scatter_kinds() == 3, buf);

    printf("1. guardar y volver a cargar\n");
    int saved = g3d_scatter_save(PATH);
    snprintf(buf, sizeof buf, "%d escritos", saved);
    check("se guarda entero", saved == N, buf);

    /* Copia exacta de lo sembrado, por especie, para comparar tras recargar. */
    static float ref[3][600][5];
    static int   refn[3] = { 0, 0, 0 };
    for (int k = 0; k < g3d_scatter_kinds(); k++) {
        refn[k] = g3d_scatter_kind_count(k);
        for (int i = 0; i < refn[k] && i < 600; i++)
            g3d_scatter_get(k, i, ref[k][i]);
    }
    static char refname[3][64];
    for (int k = 0; k < 3; k++)
        snprintf(refname[k], sizeof refname[k], "%s", g3d_scatter_kind_asset(k));

    int loaded = g3d_scatter_load(PATH, 0.0f);
    snprintf(buf, sizeof buf, "%d leidos de %d", loaded, N);
    check("vuelven todos", loaded == N, buf);
    snprintf(buf, sizeof buf, "%d especies", g3d_scatter_kinds());
    check("y las mismas especies", g3d_scatter_kinds() == 3, buf);

    printf("2. y vuelven EN SU SITIO\n");
    int moved = 0, misnamed = 0; float worst = 0.0f;
    for (int k = 0; k < g3d_scatter_kinds() && k < 3; k++) {
        const char *nm = g3d_scatter_kind_asset(k);
        if (!nm || strcmp(nm, refname[k]) != 0) misnamed++;
        int n = g3d_scatter_kind_count(k);
        if (n != refn[k]) { moved += 1000; continue; }
        for (int i = 0; i < n && i < 600; i++) {
            float g[5];
            g3d_scatter_get(k, i, g);
            for (int c = 0; c < 5; c++) {
                float d = fabsf(g[c] - ref[k][i][c]);
                if (d > worst) worst = d;
                if (d > 1e-4f) moved++;
            }
        }
    }
    snprintf(buf, sizeof buf, "%d valores movidos, desvio maximo %.6f", moved, worst);
    check("cada arbol vuelve a su sitio exacto", moved == 0, buf);
    snprintf(buf, sizeof buf, "%d especies con el nombre cambiado", misnamed);
    check("y con su modelo, no el de otro", misnamed == 0, buf);

    printf("3. cada especie con SU viento\n");
    g3d_scatter_clear();
    for (int i = 0; i < 30; i++) g3d_scatter_add("Assets/pino.glb", i, 0, 0, 0, 1.0f);
    for (int i = 0; i < 30; i++) g3d_scatter_add("Assets/roca.glb", 0, 0, i, 0, 1.0f);
    g3d_scatter_set_kind_wind("Assets/pino.glb", 0.8f);
    g3d_scatter_set_kind_wind("Assets/roca.glb", 0.0f);
    g3d_scatter_save(PATH);
    g3d_scatter_load(PATH, 1.0f);
    float wpino = -1, wroca = -1;
    for (int k = 0; k < g3d_scatter_kinds(); k++) {
        const char *nm = g3d_scatter_kind_asset(k);
        if (nm && strstr(nm, "pino")) wpino = g3d_scatter_get_kind_wind(k);
        if (nm && strstr(nm, "roca")) wroca = g3d_scatter_get_kind_wind(k);
    }
    snprintf(buf, sizeof buf, "pino=%.2f (puesto 0.80)  roca=%.2f (puesta 0.00)", wpino, wroca);
    check("el viento vuelve por especie", fabsf(wpino - 0.8f) < 1e-4f && wroca == 0.0f, buf);
    check("y la roca no se balancea", wroca == 0.0f, NULL);

    printf("3b. y con SU distancia de dibujo\n");
    g3d_scatter_set_kind_distance("Assets/pino.glb", 600.0f);
    g3d_scatter_set_kind_distance("Assets/roca.glb", 90.0f);
    g3d_scatter_save(PATH);
    g3d_scatter_load(PATH, 1.0f);
    float dpino = -1, droca = -1;
    for (int k = 0; k < g3d_scatter_kinds(); k++) {
        const char *nm = g3d_scatter_kind_asset(k);
        if (nm && strstr(nm, "pino")) dpino = g3d_scatter_get_kind_distance(k);
        if (nm && strstr(nm, "roca")) droca = g3d_scatter_get_kind_distance(k);
    }
    snprintf(buf, sizeof buf, "pino=%.0f (puesto 600)  roca=%.0f (puesta 90)", dpino, droca);
    check("la distancia vuelve por especie",
          fabsf(dpino - 600.0f) < 0.5f && fabsf(droca - 90.0f) < 0.5f, buf);

    printf("3c. y la marca de solido\n");
    g3d_scatter_set_kind_solid("Assets/pino.glb", 1);
    g3d_scatter_set_kind_solid("Assets/roca.glb", 0);
    g3d_scatter_save(PATH);
    g3d_scatter_load(PATH, 1.0f);
    int spino = -1, sroca = -1;
    for (int k = 0; k < g3d_scatter_kinds(); k++) {
        const char *nm = g3d_scatter_kind_asset(k);
        if (nm && strstr(nm, "pino")) spino = g3d_scatter_get_kind_solid(k);
        if (nm && strstr(nm, "roca")) sroca = g3d_scatter_get_kind_solid(k);
    }
    snprintf(buf, sizeof buf, "pino=%d (puesto 1)  roca=%d (puesta 0)", spino, sroca);
    check("lo solido vuelve por especie", spino == 1 && sroca == 0, buf);

    printf("4. una siembra del formato anterior sigue abriendo\n");
    {
        /* Fichero del formato viejo, escrito a mano: sin viento por especie. */
        FILE *o = fopen("/tmp/_scatter_v1.bin", "wb");
        unsigned int nk = 1, len = 15, cnt = 2;
        fwrite("G3DSCAT1", 1, 8, o); fwrite(&nk, 4, 1, o);
        fwrite(&len, 4, 1, o); fwrite("Assets/pino.glb", 1, 15, o); fwrite(&cnt, 4, 1, o);
        float pl[10] = { 1,2,3,4,5,  6,7,8,9,10 };
        fwrite(pl, sizeof(float), 10, o); fclose(o);
    }
    int old = g3d_scatter_load("/tmp/_scatter_v1.bin", 1.0f);
    snprintf(buf, sizeof buf, "%d leidos, %d especies", old, g3d_scatter_kinds());
    check("el formato anterior no se rompe", old == 2 && g3d_scatter_kinds() == 1, buf);

    printf("5. una escena sin siembra no arrastra la anterior\n");
    g3d_scatter_clear();
    snprintf(buf, sizeof buf, "%d plantados", g3d_scatter_count());
    check("limpiar deja el campo vacio", g3d_scatter_count() == 0, buf);
    /* Guardar sin nada borra el fichero: si no, la escena siguiente cargaria la
       vegetacion de esta. */
    g3d_scatter_save(PATH);
    FILE *f = fopen(PATH, "rb");
    check("guardar en vacio borra el fichero", f == NULL, f ? "sigue ahi" : "borrado");
    if (f) fclose(f);
    int none = g3d_scatter_load(PATH, 0.0f);
    snprintf(buf, sizeof buf, "%d leidos", none);
    check("y cargar sin fichero no es un error", none == 0, buf);

    printf("\n%s (%d fallo%s)\n", fails ? "FALLOS" : "TODO OK", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
