/*
 * libmod_3d_fx.h - Efectos de particulas "de verdad": humo, llamas y chispas.
 *
 * Todo son billboards con shader (ruido fbm animado) dibujados en UNA sola llamada: el juego solo emite
 * particulas con g3d_fx_emit y el motor las mueve, las hace crecer, las colorea por edad y las desvanece.
 *   kind 0 = HUMO   (mezcla normal, ordenado de lejos a cerca). pal 0 = blanco/polvo, 1 = hollin oscuro.
 *   kind 1 = LLAMA  (mezcla brillante, color por temperatura/edad: blanco->amarillo->naranja->rojo).
 *                   pal 0 = fuego naranja, 1 = fuego azul, 2 = fuego morado. stretch > 0 estira la llama en
 *                   la direccion de su velocidad (lengua de fuego).
 *   (rvx,rvy,rvz) = velocidad de arrastre: la particula se mueve con v + rv pero se orienta/estira solo con v
 *   (asi una llama puede ir pegada a un kart que corre: rv = velocidad del kart, v = chorro hacia atras).
 *   kind 2 = CHISPA (punto brillante estirado segun la velocidad). pal 0 amarillo, 1 azul, 2 naranja, 3 morado.
 */
#ifndef __LIBMOD_3D_FX_H
#define __LIBMOD_3D_FX_H
#include "libmod_3d_camera.h"
#ifdef __cplusplus
extern "C" {
#endif
void g3d_fx_emit(int kind, int pal, float x, float y, float z, float vx, float vy, float vz,
                 float life, float size0, float size1, float alpha, float stretch, float rvx, float rvy, float rvz);
void g3d_fx_clear(void);
void g3d_fx_render(G3DCamera *camera, int flip_y);
#ifdef __cplusplus
}
#endif
#endif
