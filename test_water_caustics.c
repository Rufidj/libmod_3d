/* Las causticas deben ANADIR luz al fondo sumergido, y solo ahi. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_camera.h"
#define S 129
#define WS 200.0f
#define VW 640
#define VH 400
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
static const char*qv="#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 uVP;\nvoid main(){gl_Position=uVP*vec4(p,1.0);}\n";
static const char*qf="#version 330 core\nout vec4 c;\nvoid main(){c=vec4(0.25,0.22,0.18,1.0);}\n";
static double luma(unsigned char*px){ double s=0; for(int i=0;i<VW*VH;i++) s+=px[i*4]*0.3+px[i*4+1]*0.59+px[i*4+2]*0.11; return s/(VW*VH); }
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("c",0,0,VW,VH,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    SDL_GLContext gc=SDL_GL_CreateContext(w);
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
    /* cuenca llena de agua: fondo a -8, mar a 0 */
    /* Bajio somero, como el terreno del editor: aqui la absorcion apenas
       atenua y las causticas llegan a plena intensidad. Con el fondo a 8 de
       profundidad el fallo NO se reproduce -- la absorcion lo tapa. */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++) terr[j*S+i]=-0.45f;
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(2.0f);
    G3DCamera*cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(0,14,40); cam->fov=60; cam->near_plane=0.1f;
    cam->far_plane=600; cam->aspect_ratio=(float)VW/VH;
    g3d_camera_look_at_impl(cam,vec3_make(0,-0.4f,0),vec3_make(0,1,0));
    g3d_camera_update(cam);
    glViewport(0,0,VW,VH);
    /* --- fondo marino opaco --- */
    GLuint p=glCreateProgram(),vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&qv,0); glCompileShader(vs); glShaderSource(fs,1,&qf,0); glCompileShader(fs);
    glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    float q[]={-90,-0.45f,-90, 90,-0.45f,-90, 90,-0.45f,90, -90,-0.45f,-90, 90,-0.45f,90, -90,-0.45f,90};
    GLuint va,vb; glGenVertexArrays(1,&va); glGenBuffers(1,&vb);
    glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER,vb);
    glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    Mat4 vp=mat4_multiply(g3d_camera_get_projection(cam),g3d_camera_get_view(cam));
    unsigned char*px=malloc(VW*VH*4); double lum[2];
    for(int pass=0;pass<2;pass++){
        g3d_water_render_set_caustics(pass==0?0.0f:1.6f);
        glClearColor(0.05f,0.07f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
        glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,6); glBindVertexArray(0);
        g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
        g3d_water_render(cam,0);
        glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
        lum[pass]=luma(px);
        if(pass==1){ FILE*f=fopen("/tmp/caust.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",VW,VH);
            for(int y=VH-1;y>=0;y--)for(int x=0;x<VW;x++)fwrite(&px[(y*VW+x)*4],1,3,f); fclose(f); }
    }
    /* EL FALLO REAL: las causticas SOLO SUMAN luz, asi que si su patron se
       vuelve coherente sobre un area grande el mar entero se va a blanco. Con
       dos capas de ruido de escala parecida eso ocurre por BATIDO, y la mancha
       barre la escena cada pocos segundos. Un solo fotograma no lo ve: hay que
       recorrer el tiempo y quedarse con el peor. */
    printf("0. las causticas nunca blanquean el mar\n");
    {
        long worst=0;
        g3d_water_render_set_caustics(1.6f);
        for(int fr=0; fr<90; fr++){
            glClearColor(0.05f,0.07f,0.10f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
            glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
            glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,6); glBindVertexArray(0);
            g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
            g3d_water_render(cam,0);
            glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
            long n=0;
            for(int i=0;i<VW*VH;i++)
                if(px[i*4]>200 && px[i*4+1]>200 && px[i*4+2]>200) n++;
            if(n>worst) worst=n;
            SDL_Delay(12);
        }
        char bb[160];
        snprintf(bb,sizeof bb,"maximo %ld de %d pixeles casi blancos (%.1f%%)",
                 worst,VW*VH,100.0*worst/(VW*VH));
        check("ningun instante blanquea el mar", worst < (long)VW*VH/10, bb);
        g3d_water_render_set_caustics(1.6f);
    }

    char b[160];
    printf("1. el pase de causticas\n");
    snprintf(b,sizeof b,"luma %.2f -> %.2f",lum[0],lum[1]);
    check("anaden luz al fondo sumergido", lum[1]>lum[0]+0.4, b);
    check("no la quitan (solo suman)", lum[1]>=lum[0], "");
    int e=0; while(glGetError()!=GL_NO_ERROR) e++;
    check("sin errores GL", e==0, "");
    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
