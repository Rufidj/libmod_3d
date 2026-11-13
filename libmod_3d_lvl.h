/*
 * libmod_3d_lvl.h - formato binario de niveles (.lvl) del modo Plataformas / Pasillo.
 *
 * Un fichero guarda TODO el nivel: todos sus planos (plano jugable, planos al fondo...) con sus capas de tiles, y deja sitio
 * para lo que viene (objetos, luces, zonas, disparadores) en bloques propios. Lo leen y escriben a la vez el editor y el juego.
 *
 * Formato (little endian):
 *   cabecera : "BLVL" | u16 version | u16 reservado
 *   bloques  : { char tag[4] | u32 tamano | datos[tamano] } ... hasta el final del fichero
 *              un lector IGNORA los bloques que no conoce (asi se pueden anadir tipos sin romper versiones viejas)
 *   "PLAN"   : u16 indice del plano | u16 ancho | u16 alto | u8 capas | capas * (ancho*alto) bytes (fila 0 = fila de ARRIBA del mapa)
 *              capas 0..2 = R, G, B del PNG antiguo; 3..5 = <nivel>.capas.png
 */
#ifndef LIBMOD_3D_LVL_H
#define LIBMOD_3D_LVL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G3D_LVL_MAX_PLANES 8
#define G3D_LVL_LAYERS     8
#define G3D_LVL_VERSION    1

typedef struct G3DLvl G3DLvl;

G3DLvl *g3d_lvl_new(void);
void    g3d_lvl_free(G3DLvl *l);
G3DLvl *g3d_lvl_load(const char *path);                 /* NULL si no existe o no es valido */
int     g3d_lvl_save(const G3DLvl *l, const char *path); /* 1 = bien */

int     g3d_lvl_plane_count(const G3DLvl *l);            /* hay planos 0..count-1 */
int     g3d_lvl_plane_exists(const G3DLvl *l, int plane);
int     g3d_lvl_width(const G3DLvl *l, int plane);
int     g3d_lvl_height(const G3DLvl *l, int plane);
/* crea (o redimensiona vaciando) un plano */
int     g3d_lvl_plane_alloc(G3DLvl *l, int plane, int w, int h);
void    g3d_lvl_plane_remove(G3DLvl *l, int plane);
/* puntero a las celdas de una capa (w*h bytes) para edicion en bloque; NULL si no existe */
uint8_t *g3d_lvl_layer(G3DLvl *l, int plane, int layer);
int     g3d_lvl_cell(const G3DLvl *l, int plane, int layer, int x, int y);   /* 0 si esta fuera */

/* Enlaces entre objetos (bloque LNKS): texto, una linea por objeto  plano|x|y|nombre|destinos  (destinos separados por comas).
   Puertas, botones, dianas y portales se nombran y se enlazan por su nombre. Cadena vacia = ninguno. */
const char *g3d_lvl_links(const G3DLvl *l);
void        g3d_lvl_set_links(G3DLvl *l, const char *text);

#ifdef __cplusplus
}
#endif
#endif
