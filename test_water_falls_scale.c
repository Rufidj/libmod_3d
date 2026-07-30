/* Escala REAL del editor: grid 160 sobre 400 unidades -> celda 2.5 */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_water_falls.h"
#include "libmod_3d_camera.h"
#define S 161
#define WS 400.0f
static float terr[S*S];
static float wx(int i){return ((float)i/(float)(S-1)-0.5f)*WS;}
static G3DCamera*cam;
static int run(const char*name){
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_evaporation(0.0f);
    g3d_waterfield_add_spring(wx(12),0.0f,25.0f);
    g3d_waterfield_settle(150.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam,0);
    int n=g3d_water_falls_count();
    { const float*dd=g3d_waterfield_depth_array(); const float*tt=g3d_waterfield_terrain_array();
      printf("     perfil: ");
      for(int i=70;i<=77;i++) printf("i%d t=%.1f d=%.4f  ",i,tt[80*S+i],dd[80*S+i]);
      printf("\n"); }
    printf("  %-28s -> %d laminas\n",name,n);
    g3d_waterfield_shutdown();
    return n;
}
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_Window*w=SDL_CreateWindow("e",0,0,320,240,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    SDL_GLContext c=SDL_GL_CreateContext(w);
    cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(0,60,150); cam->fov=60; cam->near_plane=0.1f;
    cam->far_plane=1200; cam->aspect_ratio=1.33f;
    g3d_camera_look_at_impl(cam,vec3_make(0,0,0),vec3_make(0,1,0));
    g3d_camera_update(cam);
    glViewport(0,0,320,240);

    printf("A escala del editor (celda %.2f u):\n", WS/(S-1));
    /* 1. ladera SUAVE: 40 unidades de caida en 100 celdas (~9 grados) */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1);
        terr[j*S+i]= 40.0f - 40.0f*u;
    }
    int gentle=run("ladera suave (~9 grados)");

    /* 2. acantilado real: 30 unidades en 2 celdas (~80 grados) */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1); float h;
        if(u<0.45f) h=40.0f-10.0f*u;
        else if(u<0.4625f) h=35.5f-30.0f*((u-0.45f)/0.0125f);
        else h=5.5f-4.0f*(u-0.4625f);
        terr[j*S+i]=h;
    }
    int cliff=run("acantilado (30u en 2 celdas)");

    printf("\n%s\n", (gentle==0 && cliff>0) ? "CORRECTO: solo el acantilado genera cascada"
                                            : "MAL: criterio de pendiente incorrecto");
    return (gentle==0&&cliff>0)?0:1;
}
