/*
 * libmod_3d_weather.h - Clima: nieve y lluvia.
 *
 * Todo lo hace la GPU: cada particula se calcula en el shader a partir de su numero y del tiempo (no hay que moverlas desde la
 * CPU ni emitirlas), y se repiten en una caja que sigue a la camara (lo que sale por un lado entra por el otro): siempre hay
 * nieve o lluvia alrededor sin importar cuanto se mueva la camara. Se dibuja con prueba de profundidad: las paredes y los techos
 * las tapan.
 *   tipo 1 = NIEVE  (copos que caen despacio, se mecen y los lleva el viento)
 *   tipo 2 = LLUVIA (rayas verticales rapidas)
 *   intensidad 0..1 = cuantas particulas se ven (0 = nada, se puede subir y bajar poco a poco para entrar y salir de la zona)
 */
#ifndef __LIBMOD_3D_WEATHER_H
#define __LIBMOD_3D_WEATHER_H
#include "libmod_3d_camera.h"
#ifdef __cplusplus
extern "C" {
#endif
void g3d_weather_set(int type, float intensity);                 /* type: 0 nada, 1 nieve, 2 lluvia */
void g3d_weather_set_wind(float wx, float wz);                   /* viento horizontal en m/s */
/* size_mul: tamano de cada particula (1 = normal); speed_mul: velocidad de caida (1 = normal); color; area: lado de la caja en m (50 por defecto) */
void g3d_weather_set_style(float size_mul, float speed_mul, float r, float g, float b, float area);
void g3d_weather_render(G3DCamera *camera, int flip_y);
#ifdef __cplusplus
}
#endif
#endif
