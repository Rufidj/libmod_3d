/*
 * libmod_3d_scatter.h - Scattered vegetation and props
 *
 * What stops a terrain looking generated is rarely the terrain: it is what
 * grows on it. A slope with grass in the flats, rocks where it steepens and
 * nothing below the waterline reads as a place; the same slope bare reads as a
 * mesh.
 *
 * Scattering means thousands of copies, so each kind becomes ONE instance group
 * (see libmod_3d_instance.h) drawn in a single call with distance and frustum
 * culling. Placing them as ordinary entities would mean thousands of draw calls
 * and a scene file to match.
 *
 * The placements live in their own file next to the scene, not in the scene
 * text: ten thousand trees would drown it, and a game only needs one line to
 * load them back.
 */

#ifndef __LIBMOD_3D_SCATTER_H
#define __LIBMOD_3D_SCATTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Carpeta desde la que resolver las rutas de los assets. Lo GUARDADO es siempre
   una ruta relativa al proyecto ("Assets/pino.glb"), que es lo que necesita un
   juego: se ejecuta desde la carpeta del proyecto. El editor no -- corre desde
   su propio directorio -- asi que le dice aqui donde esta el proyecto. Con eso
   el mismo fichero vale para los dos. */
void g3d_scatter_set_base(const char *dir);

/* Drop everything scattered (and its instance groups). */
void g3d_scatter_clear(void);

/* Record one placement of `asset` (a path relative to the project, e.g.
   "Assets/tree.glb"). Returns the index within its kind, or -1.
   This only stores it -- nothing is drawn until g3d_scatter_build(). */
int g3d_scatter_add(const char *asset, float x, float y, float z,
                    float yaw_deg, float scale);

/* Balanceo de una ESPECIE. Un pino se mece; una roca no. Aplicarlo al sembrado
   entero por igual es lo que dejaba las piedras balanceandose. */
void  g3d_scatter_set_kind_wind(const char *asset, float wind);
float g3d_scatter_get_kind_wind(int kind);

/* A que distancia deja de dibujarse una especie (0 = lo que traiga el motor,
   250 unidades). Junto con g3d_instances_set_lod_distance -- que cambia a una
   malla de bajo poligono a lo lejos y viene APAGADA -- es lo que sostiene un
   bosque de miles de copias. */
/* Si una especie bloquea el paso. Los colisionadores estaticos del motor son un
   array acotado (512 cajas), asi que esto es para troncos y rocas grandes, no
   para hierba: g3d_scatter_solid_placed() dice cuantos entraron de verdad. */
/* Indice de una especie por su asset (la crea si hace falta). Es lo que ata un
   PROCESS de BennuGD a su bosque: se guarda en la local `entity`. */
int  g3d_scatter_group(const char *asset);

/* Vuelca los ajustes de una especie por indice. Lo usa el hook de procesos en
   cada frame, con los locales wind / draw_dist / solid. */
void g3d_scatter_kind_apply(int kind, float wind, float dist, int solid);

void g3d_scatter_set_kind_solid(const char *asset, int solid);
int  g3d_scatter_get_kind_solid(int kind);
int  g3d_scatter_solid_placed(void);

/* Editar un ejemplar ya sembrado: moverlo, girarlo, cambiarle el tamano o
   quitarlo. `g3d_scatter_pick` da el mas cercano a un punto del suelo, que es
   como se seleciona uno con el raton. Tras editar hay que volver a
   g3d_scatter_build(). */
int g3d_scatter_set(int kind, int index, float x, float y, float z,
                    float yaw_deg, float scale);
int g3d_scatter_remove(int kind, int index);
int g3d_scatter_pick(float x, float z, float radius, int *out_kind, int *out_index);

void  g3d_scatter_set_kind_distance(const char *asset, float dist);
float g3d_scatter_get_kind_distance(int kind);

/* Build (or rebuild) the instance groups from what has been recorded. Loads
   each asset once. `wind_scale` multiplies the sway each kind was authored with
   (1 = as authored). Devuelve cuantas ESPECIES se montaron -- cada una puede
   ocupar varios grupos, uno por pieza del modelo. */
int g3d_scatter_build(float wind_scale);

/* Everything recorded, and how many kinds. */
int g3d_scatter_count(void);
int g3d_scatter_kinds(void);

/* Leer lo sembrado: especie por especie y colocacion por colocacion. Existe
   para poder COMPROBAR que lo guardado vuelve exacto -- un conteo correcto con
   las posiciones mal es el fallo que no se ve hasta que ya perdiste el trabajo.
   `out5` recibe { x, y, z, yaw, escala }. */
const char *g3d_scatter_kind_asset(int kind);
int  g3d_scatter_kind_count(int kind);
int  g3d_scatter_get(int kind, int index, float *out5);

/* Cuantos grupos de instancias ocupa una especie: uno por pieza del modelo
   (tronco, hojas...), saltando las cascaras de contorno. */
int  g3d_scatter_kind_groups(int kind);

/* The placements file. Binary, one block per kind. Saving with nothing
   scattered removes the file, so a scene that no longer has vegetation does not
   keep loading yesterday's. */
int g3d_scatter_save(const char *path);

/* Load and build in one go -- this is what a generated game calls. Returns the
   number of placements loaded, or 0 (no file is not an error: it just means
   this scene has nothing scattered). */
int g3d_scatter_load(const char *path, float wind);

void g3d_scatter_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SCATTER_H */
