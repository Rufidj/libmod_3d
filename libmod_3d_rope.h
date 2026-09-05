/*
 * libmod_3d_rope.h - Cuerdas: la misma simulacion que las telas, pero en una
 * dimension. Una fila de particulas unidas por distancias fijas, integrada con
 * Verlet y relajada cada frame, dibujada como un TUBO (no una linea, para que se
 * vea desde cualquier angulo). Cuelga entre dos puntos, se mueve con el viento y
 * la aparta lo que pase -- comparte los empujones de las telas.
 *
 * Sirve para tender cuerdas, cables, lianas y puentes... y para COLGAR TELAS: se
 * pregunta donde esta un punto de la cuerda y se mueve ahi la sujecion de la tela.
 */
#ifndef __LIBMOD_3D_ROPE_H
#define __LIBMOD_3D_ROPE_H
#ifdef __cplusplus
extern "C" {
#endif

/* Crea una cuerda de (ax,ay,az) a (bx,by,bz) con 'segmentos' tramos y 'grosor' de
   radio. 'holgura' es cuanto mas larga es que la distancia entre los extremos:
   0 = tirante, 0.3 = cuelga bastante. Devuelve un id, o -1. */
int  g3d_rope_create(float ax, float ay, float az,
                     float bx, float by, float bz,
                     int segmentos, float grosor, float holgura);
/* Fija o suelta un extremo: extremo 0 = el primero, 1 = el ultimo. */
void g3d_rope_pin(int rope, int extremo, int fijo);
/* Clava un punto cualquiera de la cuerda donde se le diga (un gancho que se mueve). */
void g3d_rope_pin_move(int rope, int punto, float x, float y, float z);
void g3d_rope_set_wind(int rope, float x, float y, float z, float strength);
void g3d_rope_set_texture(int rope, unsigned int gl_handle);
/* Donde esta ahora el punto n de la cuerda (para colgar cosas de el). */
int  g3d_rope_point(int rope, int punto, float *x, float *y, float *z);
int  g3d_rope_points(int rope);
void g3d_rope_update(int rope, float dt);
void g3d_rope_destroy(int rope);
void g3d_rope_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif /* __LIBMOD_3D_ROPE_H */
