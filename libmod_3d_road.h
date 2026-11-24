/*
 * libmod_3d_road.h - Carreteras: SOLO por donde se puede circular.
 *
 * Una carretera es una lista de nodos (posicion, ancho, peralte, superficie) unidos por curvas
 * suaves. El motor calcula su linea central y la puede consultar: "estoy en la carretera?",
 * "donde esta el punto a 50 m?", "cuanto mide aqui de ancho?". Todo lo demas (salida, meta,
 * vueltas, rivales, turbos...) lo hace el juego por codigo.
 *
 * Convenciones: el rumbo 0 mira hacia +Z y crece hacia +X (adelante = (sin rumbo, cos rumbo)),
 * igual que el vehiculo. "Izquierda" es la del que avanza. El peralte positivo sube el borde derecho.
 */
#ifndef __LIBMOD_3D_ROAD_H
#define __LIBMOD_3D_ROAD_H

#ifdef __cplusplus
extern "C" {
#endif

#define G3D_ROAD_MAX        8
#define G3D_ROAD_MAX_NODES  512

int   g3d_road_create(int closed);                 /* id de la carretera, o -1 */
void  g3d_road_destroy(int road);
void  g3d_road_clear(void);
/* Anade un nodo. follow=1: la carretera sigue al terreno (altura = terreno + dy); follow=0: altura fija y.
   surface es la etiqueta del tramo que SALE de este nodo (0 = asfalto, 1 = tierra, 2 = hielo, 3 = barro,
   o lo que decida el juego). Devuelve el indice del nodo, o -1. */
int   g3d_road_add_node(int road, float x, float z, float y, float width, float bank_deg,
                        int follow, float dy, int surface);
/* Tangente propia de un nodo (la direccion y "fuerza" con la que la curva pasa por el): (0, 0) = automatica, que es lo
   normal. Sirve para curvas muy cerradas o para reproducir un trazado dibujado en el editor. */
void  g3d_road_set_tangent(int road, int node, float tx, float tz);
/* Calcula la linea central. Hay que llamarla tras anadir los nodos y DESPUES de cargar el terreno si algun
   nodo lo sigue. Devuelve el numero de muestras (0 si no hay trazado valido). */
int   g3d_road_build(int road);
int   g3d_road_count(void);                        /* carreteras existentes */
int   g3d_road_nodes(int road);
int   g3d_road_closed(int road);
float g3d_road_length(int road);

/* Consultas por distancia recorrida d (m). En una carretera cerrada d da la vuelta; en una abierta se recorta. */
float g3d_road_x(int road, float d);
float g3d_road_y(int road, float d);
float g3d_road_z(int road, float d);
float g3d_road_yaw(int road, float d);             /* rumbo en radianes */
float g3d_road_width(int road, float d);
int   g3d_road_surface(int road, float d);

/* La carretera es SOLO informacion de por donde se puede circular: ni se dibuja ni modifica el suelo (el aspecto es
   el de tu terreno pintado o tu modelo, y el suelo el de tu terreno o tu colision). Lo siguiente es opcional, para
   carreteras inventadas por codigo o para depurar.

   g3d_road_show crea la malla (una por superficie) como entidades de la escena. g3d_road_entity da la entidad de una superficie para
   cambiarle material o textura (g3d_entity_set_material, g3d_material_set_texture...), o -1 si no tiene. */
int   g3d_road_show(int road, int scene);          /* devuelve el numero de entidades creadas */
void  g3d_road_hide(int road);
int   g3d_road_entity(int road, int surface);
void  g3d_road_set_color(int surface, float r, float g, float b);   /* color de serie de una superficie */

/* Altura del suelo en (x, z) SI el punto esta dentro del ancho de una carretera. No toca g3d_road_near_*.
   Devuelve 1 y deja la altura en *h; 0 si el punto no esta sobre ninguna carretera. Es lo que hace que la
   carretera cuente como suelo (g3d_scene_terrain_height la consulta). */
int   g3d_road_ground(float x, float z, float *h);
void  g3d_road_set_ground(int road, int on);    /* 1 = la carretera cuenta como suelo (g3d_scene_terrain_height); 0 por defecto */

/* Busca la carretera mas cercana a (x, z) y deja el resultado para las g3d_road_near_*.
   Devuelve el id de la carretera, o -1 si no hay ninguna. */
int   g3d_road_nearest(float x, float z);
float g3d_road_near_dist(void);                    /* distancia recorrida del punto proyectado */
float g3d_road_near_offset(void);                  /* separacion lateral al centro (+ = izquierda) */
float g3d_road_near_y(void);                       /* altura de la carretera ahi */
float g3d_road_near_width(void);
int   g3d_road_near_surface(void);
int   g3d_road_near_on(void);                      /* 1 si el punto esta dentro del ancho */
float g3d_road_near_yaw(void);

#ifdef __cplusplus
}
#endif
#endif
