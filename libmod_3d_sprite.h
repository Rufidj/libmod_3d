/*
 * libmod_3d_sprite.h - Sprites 2D dentro del mundo 3D (estilo HD-2D / Octopath).
 *
 * Un sprite es un quad que mira a la camara y se dibuja EN EL PASE OPACO con
 * recorte alfa (discard), asi que tapa y es tapado por la geometria 3D sin
 * necesidad de ordenar nada.
 *
 * La textura sale de un grafico normal de BennuGD2 (FPG/MAP/PNG ya cargado):
 * la capa de libmod_3d.c coge el GRAPH con bitmap_get(file, graph) y le pasa
 * aqui el identificador de textura de OpenGL. Por eso el sprite se anima como
 * en 2D -- cambiando 'graph' -- y no hay que cargar el grafico dos veces.
 */
#ifndef __LIBMOD_3D_SPRITE_H
#define __LIBMOD_3D_SPRITE_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Modo de encaramiento */
#define G3D_SPRITE_CYLINDRICAL  0   /* gira solo en horizontal: sigue de pie (personajes) */
#define G3D_SPRITE_SPHERICAL    1   /* mira siempre de frente a la camara (items, efectos) */

int  g3d_sprite_create(int scene_id, float x, float y, float z);
void g3d_sprite_destroy(int id);
void g3d_sprites_clear(void);
int  g3d_sprite_count(void);
int  g3d_sprite_alive(int id);

void g3d_sprite_set_position(int id, float x, float y, float z);
void g3d_sprite_get_position(int id, float *x, float *y, float *z);

/* Textura + recorte dentro de ella (u0,v0)-(u1,v1) y tamano en pixeles del
   grafico, que es lo que da la proporcion del quad. */
void g3d_sprite_set_texture(int id, unsigned int gl_tex, int px_w, int px_h,
                            float uscale, float vscale);

/* Hoja de sprites: en vez de un grafico por fotograma (FPG), un solo grafico
   partido en celdas. O rejilla regular (columnas x filas + numero de fotograma)
   o el recorte en pixeles, para hojas irregulares. */
void g3d_sprite_set_grid(int id, int cols, int rows);
void g3d_sprite_set_frame(int id, int frame);
void g3d_sprite_set_cell(int id, int x, int y, int w, int h);

/* Tamano en el mundo: o se fija el alto en unidades, o se dice cuantos pixeles
   del grafico entran en una unidad de mundo (lo comodo para pixel art). */
void g3d_sprite_set_height(int id, float world_height);
void g3d_sprite_set_pixels_per_unit(int id, float ppu);

/* Punto del grafico que se planta en x,y,z. (0.5, 1.0) = centro-abajo: los pies
   del personaje sobre el suelo. Si el grafico trae punto de control 0, la capa
   de BennuGD2 lo usa en su lugar. */
void g3d_sprite_set_anchor(int id, float ax, float ay);

void g3d_sprite_set_billboard(int id, int mode);
void g3d_sprite_set_cutout(int id, float threshold);   /* alfa por debajo = agujero */
void g3d_sprite_set_tint(int id, float r, float g, float b, float a);
void g3d_sprite_set_flip(int id, int flip_x);
void g3d_sprite_set_scale(int id, float s);            /* el local 'size' (1.0 = 100%) */
void g3d_sprite_set_visible(int id, int visible);

/* Direccion (0..ndirs-1) en la que hay que dibujar el sprite: 0 = de frente a
   la camara, y va girando. Con esto el .prg elige el grafico de sus N posturas. */
int  g3d_sprite_direction(int id, float angle_rad, int ndirs, G3DCamera *camera);

/* Dibujado (lo llama el renderer, en el pase opaco). */
void g3d_sprites_render(G3DCamera *camera, int flip_y);
/* Sombras: los mismos quads en el mapa de sombras, con el mismo recorte alfa. */
void g3d_sprites_render_depth(const float *light_view_proj16);
void g3d_sprite_set_shadow(int id, int on);
/* Filtrado: por defecto nearest (pixel art); 1 = suavizado bilineal. */
void g3d_sprite_set_smooth(int id, int on);
/* 1 (por defecto) = le afecta la luz de la escena: se apaga de noche y con el
   ciclo de dia, en vez de ir siempre a pleno brillo. */
void g3d_sprite_set_lit(int id, int on);
/* 1 (por defecto) = la posicion en pantalla se redondea a pixeles enteros, para
   que el pixel art no tiemble al moverse. */
void g3d_sprite_set_snap(int id, int on);

void g3d_sprites_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
