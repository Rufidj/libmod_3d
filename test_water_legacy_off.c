/* Nada de la API vieja debe seguir dibujando cuando existe el campo unificado. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include "libmod_3d_water.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_flow.h"
#include "libmod_3d_scenefile.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_glcaps.h"
#define S 129
#define WS 256.0f
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_Window*w=SDL_CreateWindow("l",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    SDL_GLContext c=SDL_GL_CreateContext(w);
    char buf[160];

    printf("1. un script SIN terreno pide mar -> debe salir agua nueva\n");
    g3d_water_create(0.0f, 2000.0f, 200);
    check("el campo arranca igualmente (mar abierto)", g3d_waterfield_active()==1, "");

    printf("2. con terreno: lago y rio van al campo, no a mallas viejas\n");
    g3d_waterfield_shutdown();
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float x=(float)i-64,z=(float)j-64; float r=sqrtf(x*x+z*z);
        terr[j*S+i]=(r<25.0f)?(8.0f-14.0f*(1.0f-r/25.0f)):8.0f;
    }
    G3DMesh*tm=g3d_primitive_terrain_from_heights(S,WS,terr,1.0f);
    g3d_scene_set_terrain_collider(tm);
    check("lago manual sin campo previo lo arranca",
          g3d_lake_add(0.0f,0.0f,4.0f,3.0f)==0 && g3d_waterfield_active()==1, "");
    snprintf(buf,sizeof buf,"nivel=%.2f",g3d_waterfield_level_at(0,0));
    check("el lago esta en el campo", g3d_waterfield_level_at(0,0)>G3D_NO_WATER_TEST, buf);
    check("NO se creo ninguna zona fluida vieja", g3d_fluid_count()==0, "");

    printf("3. cascada manual -> manantial en el campo, sin cintas viejas\n");
    g3d_flow_clear();
    int before=g3d_waterfield_spring_count();
    g3d_waterfall_add(20.0f, 8.0f, 0.0f, 30.0f, 0.0f, 4.0f, 0.0f);
    snprintf(buf,sizeof buf,"manantiales %d -> %d",before,g3d_waterfield_spring_count());
    check("la cascada manual crea un manantial",
          g3d_waterfield_spring_count()==before+1, buf);

    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
