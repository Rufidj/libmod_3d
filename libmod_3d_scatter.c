/*
 * libmod_3d_scatter.c - Scattered vegetation and props
 *
 * See the header. The data model is deliberately flat: a list of kinds, each
 * with an asset path and an array of placements. Building turns each kind into
 * one instance group; that is the only place the renderer is touched, so the
 * same data serves the editor's live preview and a running game.
 */

#include "libmod_3d_scatter.h"
#include "libmod_3d_instance.h"
#include "libmod_3d_gltf.h"
#include "libmod_3d_fbx.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_physics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_MAX_KINDS 64
#define SC_MAX_SUB   64   /* submallas por modelo */
#define SC_MAGIC  "G3DSCAT4"
#define SC_MAGIC3 "G3DSCAT3"   /* sin marca de solido */
#define SC_MAGIC2 "G3DSCAT2"   /* sin distancia de dibujo */
#define SC_MAGIC1 "G3DSCAT1"   /* formato viejo: sin viento por especie */

typedef struct {
    float x, y, z, yaw, scale;
} SCPlace;

typedef struct {
    char    *asset;
    SCPlace *items;
    int      count, cap;
    /* UN grupo por submalla. Un modelo de verdad viene partido por materiales
       -- una palmera trae tronco y hojas por separado, y instanciar solo la
       primera submalla dibujaba palmeras sin tronco. */
    int      groups[SC_MAX_SUB];
    int      ngroups;
    float    wind;      /* balanceo PROPIO de esta especie */
    float    dist;      /* a partir de aqui no se dibuja (0 = lo que traiga
                           el motor, 250 unidades) */
    int      solid;     /* 1 = cada ejemplar bloquea el paso */
    float    bmin[3], bmax[3];   /* caja del modelo, para los colisionadores */
    int      has_box;
} SCKind;

static SCKind  g_kind[SC_MAX_KINDS];
static int     g_kinds = 0;
static char    g_base[1024] = "";
static int     g_solid_placed = 0;

void g3d_scatter_set_base(const char *dir) {
    if (!dir) { g_base[0] = 0; return; }
    snprintf(g_base, sizeof(g_base), "%s", dir);
    size_t n = strlen(g_base);
    while (n > 0 && (g_base[n-1] == '/' || g_base[n-1] == '\\')) g_base[--n] = 0;
}

/* Carga el modelo por extension: el proyecto mezcla .glb y .fbx, y cargar un
   FBX con el lector de glTF simplemente devuelve NULL -- y entonces no aparece
   nada, sin decir por que. */
static G3DModel *sc_load_model(const char *asset) {
    char path[1200];
    if (g_base[0]) snprintf(path, sizeof(path), "%s/%s", g_base, asset);
    else           snprintf(path, sizeof(path), "%s", asset);
    const char *dot = strrchr(asset, '.');
    if (dot && (strcmp(dot, ".fbx") == 0 || strcmp(dot, ".FBX") == 0))
        return g3d_fbx_load(path);
    return g3d_gltf_load(path);
}

static SCKind *sc_find(const char *asset, int create) {
    for (int i = 0; i < g_kinds; i++)
        if (g_kind[i].asset && strcmp(g_kind[i].asset, asset) == 0) return &g_kind[i];
    if (!create || g_kinds >= SC_MAX_KINDS) return NULL;
    SCKind *k = &g_kind[g_kinds];
    k->asset = strdup(asset);
    if (!k->asset) return NULL;
    k->items = NULL; k->count = 0; k->cap = 0; k->ngroups = 0; k->wind = 0.0f; k->dist = 0.0f;
    k->solid = 0; k->has_box = 0;
    g_kinds++;
    return k;
}

void g3d_scatter_clear(void) {
    for (int i = 0; i < g_kinds; i++) {
        for (int g = 0; g < g_kind[i].ngroups; g++) g3d_instances_clear(g_kind[i].groups[g]);
        free(g_kind[i].asset);
        free(g_kind[i].items);
    }
    memset(g_kind, 0, sizeof(g_kind));
    g_kinds = 0;
}

int g3d_scatter_add(const char *asset, float x, float y, float z,
                    float yaw_deg, float scale) {
    if (!asset || !*asset) return -1;
    SCKind *k = sc_find(asset, 1);
    if (!k) return -1;
    if (k->count >= k->cap) {
        int cap = k->cap ? k->cap * 2 : 256;
        SCPlace *n = (SCPlace *)realloc(k->items, (size_t)cap * sizeof(SCPlace));
        if (!n) return -1;
        k->items = n; k->cap = cap;
    }
    SCPlace *p = &k->items[k->count];
    p->x = x; p->y = y; p->z = z; p->yaw = yaw_deg; p->scale = scale;
    return k->count++;
}

/* El viento es de la ESPECIE, no del sembrado entero: un pino se mece y una
   roca no. Aplicarlo a todo por igual daba piedras balanceandose. */
void g3d_scatter_set_kind_wind(const char *asset, float wind) {
    SCKind *k = sc_find(asset, 1);
    if (k) k->wind = wind < 0.0f ? 0.0f : wind;
}

/* Distancia de dibujo de la especie. Una hierba no hace falta verla a 300
   unidades y un arbol si: por especie es donde de verdad se gana rendimiento
   sin que se note un recorte. */
void g3d_scatter_set_kind_distance(const char *asset, float dist) {
    SCKind *k = sc_find(asset, 1);
    if (k) k->dist = dist < 0.0f ? 0.0f : dist;
}

/* Si una especie bloquea el paso. Los colisionadores estaticos del motor son un
   array acotado (512), asi que esto NO es para hierba: es para troncos y rocas
   grandes, donde de verdad importa no atravesarlos. */
void g3d_scatter_set_kind_solid(const char *asset, int solid) {
    SCKind *k = sc_find(asset, 1);
    if (k) k->solid = solid ? 1 : 0;
}

/* Indice de una especie por su asset, creandola si no existe. Es lo que ata un
   PROCESS de BennuGD a su bosque: el proceso guarda ese indice en `entity` y a
   partir de ahi el hook le vuelca sus locales cada frame. */
int g3d_scatter_group(const char *asset) {
    SCKind *k = sc_find(asset, 1);
    if (!k) return -1;
    return (int)(k - g_kind);
}

/* Ajustes por INDICE, que es lo que tiene el hook a mano. */
void g3d_scatter_kind_apply(int kind, float wind, float dist, int solid) {
    if (kind < 0 || kind >= g_kinds) return;
    SCKind *k = &g_kind[kind];
    int changed = (k->solid != (solid ? 1 : 0)) || (k->dist != dist);
    k->wind  = wind < 0.0f ? 0.0f : wind;
    k->dist  = dist < 0.0f ? 0.0f : dist;
    k->solid = solid ? 1 : 0;
    /* El viento y la distancia se aplican al vuelo; lo solido cambia los
       colisionadores y eso pide reconstruir. */
    for (int g = 0; g < k->ngroups; g++) {
        g3d_instances_set_wind(k->groups[g], k->wind);
        if (k->dist > 0.0f) g3d_instances_set_distance(k->groups[g], k->dist);
    }
    (void)changed;
}

int g3d_scatter_get_kind_solid(int kind) {
    if (kind < 0 || kind >= g_kinds) return 0;
    return g_kind[kind].solid;
}

/* Mover / reescalar / girar un ejemplar ya sembrado. */
int g3d_scatter_set(int kind, int index, float x, float y, float z,
                    float yaw_deg, float scale) {
    if (kind < 0 || kind >= g_kinds) return 0;
    if (index < 0 || index >= g_kind[kind].count) return 0;
    SCPlace *p = &g_kind[kind].items[index];
    p->x = x; p->y = y; p->z = z; p->yaw = yaw_deg; p->scale = scale;
    return 1;
}

int g3d_scatter_remove(int kind, int index) {
    if (kind < 0 || kind >= g_kinds) return 0;
    SCKind *k = &g_kind[kind];
    if (index < 0 || index >= k->count) return 0;
    /* El ultimo ocupa el hueco: el orden no significa nada y asi no hay que
       mover el resto del array. */
    k->items[index] = k->items[k->count - 1];
    k->count--;
    return 1;
}

/* El ejemplar mas cercano a un punto del suelo, dentro de `radius`. Es como se
   selecciona uno con el raton. */
int g3d_scatter_pick(float x, float z, float radius, int *out_kind, int *out_index) {
    float best = radius * radius;
    int bk = -1, bi = -1;
    for (int k = 0; k < g_kinds; k++)
        for (int i = 0; i < g_kind[k].count; i++) {
            SCPlace *p = &g_kind[k].items[i];
            float dx = p->x - x, dz = p->z - z;
            float d2 = dx*dx + dz*dz;
            if (d2 < best) { best = d2; bk = k; bi = i; }
        }
    if (bk < 0) return 0;
    if (out_kind)  *out_kind  = bk;
    if (out_index) *out_index = bi;
    return 1;
}

float g3d_scatter_get_kind_distance(int kind) {
    if (kind < 0 || kind >= g_kinds) return 0.0f;
    return g_kind[kind].dist;
}

float g3d_scatter_get_kind_wind(int kind) {
    if (kind < 0 || kind >= g_kinds) return 0.0f;
    return g_kind[kind].wind;
}

int g3d_scatter_build(float wind_scale) {
    int built = 0;
    for (int i = 0; i < g_kinds; i++) {
        SCKind *k = &g_kind[i];
        if (!k->asset || k->count <= 0) continue;

        /* Reconstruir vacia los grupos que ya hay en vez de crear otros:
           pintar es iterativo, y un grupo nuevo por pincelada dejaria una
           llamada de dibujo y un buffer colgando en cada trazo. */
        if (k->ngroups > 0) {
            for (int g = 0; g < k->ngroups; g++) g3d_instances_clear(k->groups[g]);
        } else {
            G3DModel *m = sc_load_model(k->asset);
            if (!m || m->mesh_count == 0) continue;
            for (unsigned int sm = 0; sm < m->mesh_count && k->ngroups < SC_MAX_SUB; sm++) {
                /* Las submallas de CONTORNO son una copia negra e inflada del
                   modelo, para el borde estilo comic. Instanciadas normalmente
                   taparian de negro todo lo demas. */
                if (m->mesh_outline && m->mesh_outline[sm]) continue;
                void *tex = NULL;
                if (m->mesh_textures && m->mesh_textures[sm]) tex = m->mesh_textures[sm];
                else if (m->albedo_texture)                   tex = m->albedo_texture;
                int g = g3d_instances_create(&m->meshes[sm], tex);
                if (g < 0) continue;
                g3d_instances_set_alpha_cut(g, 1);
                k->groups[k->ngroups++] = g;
            }
            /* La caja del modelo, para los colisionadores. Se guarda al cargar
               porque despues ya no se vuelve a abrir el fichero. */
            k->bmin[0] = m->aabb_min[0]; k->bmin[1] = m->aabb_min[1]; k->bmin[2] = m->aabb_min[2];
            k->bmax[0] = m->aabb_max[0]; k->bmax[1] = m->aabb_max[1]; k->bmax[2] = m->aabb_max[2];
            k->has_box = 1;
            if (k->ngroups == 0) continue;
        }
        for (int g = 0; g < k->ngroups; g++)
            for (int j = 0; j < k->count; j++) {
                SCPlace *p = &k->items[j];
                g3d_instances_add(k->groups[g], p->x, p->y, p->z, p->yaw, p->scale);
            }
        for (int g = 0; g < k->ngroups; g++) {
            g3d_instances_set_wind(k->groups[g], k->wind * wind_scale);
            if (k->dist > 0.0f) g3d_instances_set_distance(k->groups[g], k->dist);
        }
        built++;
    }

    /* Colisionadores de lo que sea solido. El array del motor esta acotado, asi
       que se avisa por el valor de retorno de cuantos entraron -- fallar en
       silencio dejaria arboles atravesables sin decir por que. */
    g_solid_placed = 0;
    for (int i = 0; i < g_kinds; i++) {
        SCKind *k = &g_kind[i];
        if (!k->solid || !k->has_box || k->count <= 0) continue;
        for (int j = 0; j < k->count; j++) {
            SCPlace *p = &k->items[j];
            /* La caja se estrecha al 60% en horizontal: la del modelo abarca la
               copa entera, y un jugador no choca con las hojas de un arbol. */
            float sx = (k->bmax[0] - k->bmin[0]) * 0.5f * p->scale * 0.6f;
            float sz = (k->bmax[2] - k->bmin[2]) * 0.5f * p->scale * 0.6f;
            float hy = (k->bmax[1] - k->bmin[1]) * p->scale;
            if (g3d_collider_add_box(p->x - sx, p->y, p->z - sz,
                                     p->x + sx, p->y + hy, p->z + sz) < 0)
                return built;                  /* sin sitio: se para aqui */
            g_solid_placed++;
        }
    }
    return built;
}

int g3d_scatter_solid_placed(void) { return g_solid_placed; }

int g3d_scatter_count(void) {
    int n = 0;
    for (int i = 0; i < g_kinds; i++) n += g_kind[i].count;
    return n;
}

int g3d_scatter_kinds(void) { return g_kinds; }

const char *g3d_scatter_kind_asset(int kind) {
    if (kind < 0 || kind >= g_kinds) return NULL;
    return g_kind[kind].asset;
}

int g3d_scatter_kind_groups(int kind) {
    if (kind < 0 || kind >= g_kinds) return 0;
    return g_kind[kind].ngroups;
}

int g3d_scatter_kind_count(int kind) {
    if (kind < 0 || kind >= g_kinds) return 0;
    return g_kind[kind].count;
}

int g3d_scatter_get(int kind, int index, float *out5) {
    if (kind < 0 || kind >= g_kinds || !out5) return 0;
    if (index < 0 || index >= g_kind[kind].count) return 0;
    SCPlace *p = &g_kind[kind].items[index];
    out5[0] = p->x; out5[1] = p->y; out5[2] = p->z;
    out5[3] = p->yaw; out5[4] = p->scale;
    return 1;
}

int g3d_scatter_save(const char *path) {
    if (!path) return 0;
    if (g3d_scatter_count() <= 0) { remove(path); return 0; }

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(SC_MAGIC, 1, 8, f);
    unsigned int nk = 0;
    for (int i = 0; i < g_kinds; i++) if (g_kind[i].count > 0) nk++;
    fwrite(&nk, sizeof(unsigned int), 1, f);

    int total = 0;
    for (int i = 0; i < g_kinds; i++) {
        SCKind *k = &g_kind[i];
        if (k->count <= 0) continue;
        unsigned int len = (unsigned int)strlen(k->asset);
        unsigned int cnt = (unsigned int)k->count;
        fwrite(&len, sizeof(unsigned int), 1, f);
        fwrite(k->asset, 1, len, f);
        fwrite(&cnt, sizeof(unsigned int), 1, f);
        fwrite(&k->wind, sizeof(float), 1, f);
        fwrite(&k->dist, sizeof(float), 1, f);
        fwrite(&k->solid, sizeof(int), 1, f);
        fwrite(k->items, sizeof(SCPlace), (size_t)k->count, f);
        total += k->count;
    }
    fclose(f);
    return total;
}

int g3d_scatter_load(const char *path, float wind) {
    g3d_scatter_clear();
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;                       /* sin fichero: escena sin siembra */

    char magic[8];
    unsigned int nk = 0;
    if (fread(magic, 1, 8, f) != 8 || fread(&nk, sizeof(unsigned int), 1, f) != 1) {
        fclose(f); return 0;
    }
    /* Se acepta el formato anterior, que no traia viento por especie: una siembra
       ya hecha no puede dejar de abrirse porque el formato haya crecido. */
    int has_solid = (memcmp(magic, SC_MAGIC,  8) == 0);
    int has_dist  = has_solid || (memcmp(magic, SC_MAGIC3, 8) == 0);
    int has_wind  = has_dist  || (memcmp(magic, SC_MAGIC2, 8) == 0);
    if (!has_wind && memcmp(magic, SC_MAGIC1, 8) != 0) { fclose(f); return 0; }
    if (nk > SC_MAX_KINDS) nk = SC_MAX_KINDS;

    int total = 0;
    for (unsigned int i = 0; i < nk; i++) {
        unsigned int len = 0, cnt = 0;
        if (fread(&len, sizeof(unsigned int), 1, f) != 1 || len == 0 || len > 4096) break;
        char *name = (char *)malloc(len + 1);
        if (!name) break;
        if (fread(name, 1, len, f) != len) { free(name); break; }
        name[len] = 0;
        if (fread(&cnt, sizeof(unsigned int), 1, f) != 1) { free(name); break; }
        float kw = 0.0f;
        if (has_wind && fread(&kw, sizeof(float), 1, f) != 1) { free(name); break; }
        g3d_scatter_set_kind_wind(name, kw);
        float kd = 0.0f;
        if (has_dist && fread(&kd, sizeof(float), 1, f) != 1) { free(name); break; }
        g3d_scatter_set_kind_distance(name, kd);
        int ks = 0;
        if (has_solid && fread(&ks, sizeof(int), 1, f) != 1) { free(name); break; }
        g3d_scatter_set_kind_solid(name, ks);
        for (unsigned int j = 0; j < cnt; j++) {
            SCPlace p;
            if (fread(&p, sizeof(SCPlace), 1, f) != 1) break;
            if (g3d_scatter_add(name, p.x, p.y, p.z, p.yaw, p.scale) >= 0) total++;
        }
        free(name);
    }
    fclose(f);
    g3d_scatter_build(wind);
    return total;
}

void g3d_scatter_shutdown(void) { g3d_scatter_clear(); }
