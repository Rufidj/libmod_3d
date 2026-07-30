/* El agua rodeada de relieve debe SEGUIR VIENDOSE.
   La superficie se atenua donde el agua cae (una cascada la dibuja aparte), y ese
   test de pendiente se midio una vez contra celdas SECAS -- que guardan la altura
   del terreno, no un nivel de agua. Junto a cualquier orilla con relieve eso
   parece un desnivel enorme, y en una escena de charcas entre colinas el agua
   desaparecia entera. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_camera.h"
#define S 161
#define WS 400.0f
#define VW 640
#define VH 400
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("s",0,0,VW,VH,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    SDL_GLContext c=SDL_GL_CreateContext(w);
    GLuint fbo,col,dep;
    glGenTextures(1,&col); glBindTexture(GL_TEXTURE_2D,col);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,VW,VH,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenRenderbuffers(1,&dep); glBindRenderbuffer(GL_RENDERBUFFER,dep);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,VW,VH);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,col,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,dep);

    /* colinas marcadas con valles entre ellas: mucha orilla con relieve */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float x=i*0.16f, z=j*0.16f;
        terr[j*S+i] = 9.0f*sinf(x)*cosf(z) + 4.0f*sinf(x*2.3f+1.7f);
    }
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);   /* solo se llenan los valles */
    g3d_waterfield_settle(6.0f);

    G3DCamera*cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(0,70,120); cam->fov=60; cam->near_plane=0.1f;
    cam->far_plane=900; cam->aspect_ratio=(float)VW/VH;
    g3d_camera_look_at_impl(cam,vec3_make(0,0,0),vec3_make(0,1,0));
    g3d_camera_update(cam);
    glViewport(0,0,VW,VH);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam,0);
    unsigned char*px=malloc(VW*VH*4);
    glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
    long lit=0; for(int i=0;i<VW*VH;i++) if(px[i*4]+px[i*4+1]+px[i*4+2] > 24) lit++;
    char b[160];
    double pct=100.0*lit/(VW*VH);
    snprintf(b,sizeof b,"%.1f%% del frame",pct);
    printf("1. charcas entre colinas\n");
    check("el agua entre relieve se sigue viendo", pct > 8.0, b);
    FILE*f=fopen("/tmp/shore.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",VW,VH);
    for(int y=VH-1;y>=0;y--)for(int x=0;x<VW;x++)fwrite(&px[(y*VW+x)*4],1,3,f); fclose(f);
    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
