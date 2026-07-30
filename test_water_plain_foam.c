/* Reproduce el caso del editor: llano + manantial. NO debe salir blanco. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_camera.h"
#define S 161
#define WS 400.0f
#define VW 640
#define VH 400
static float terr[S*S];
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("p",0,0,VW,VH,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
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
    /* llano practicamente plano, como el terreno por defecto del editor */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++) terr[j*S+i]=0.2f*sinf(i*0.05f)*cosf(j*0.04f);
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_add_spring(0.0f,0.0f,10.0f);
    g3d_waterfield_settle(60.0f);
    G3DCamera*cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(0,55,120); cam->fov=60; cam->near_plane=0.1f;
    cam->far_plane=900; cam->aspect_ratio=(float)VW/VH;
    g3d_camera_look_at_impl(cam,vec3_make(0,0,0),vec3_make(0,1,0));
    g3d_camera_update(cam);
    glViewport(0,0,VW,VH);
    glClearColor(0.10f,0.35f,0.10f,1.0f);   /* verde = "terreno" */
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam,0);
    unsigned char*px=malloc(VW*VH*4);
    glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
    long white=0,tot=VW*VH;
    for(int i=0;i<tot;i++)
        if(px[i*4]>170&&px[i*4+1]>170&&px[i*4+2]>170) white++;
    printf("pixeles blancos (espuma): %ld de %ld = %.2f%%\n",white,tot,100.0*white/tot);
    FILE*f=fopen("/tmp/plain.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",VW,VH);
    for(int y=VH-1;y>=0;y--)for(int x=0;x<VW;x++)fwrite(&px[(y*VW+x)*4],1,3,f);
    fclose(f);
    return white*100/tot > 5 ? 1 : 0;
}
