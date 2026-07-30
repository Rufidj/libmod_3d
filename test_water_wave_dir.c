/* Sigue la LINEA DE ESPUMA (lo mas brillante) a lo largo del tiempo. La espuma
   solo la produce la rompiente, asi que aisla el oleaje de playa del FFT. */
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
#define S 161
#define WS 400.0f
#define VW 512
#define VH 320
static float terr[S*S];
static const char*qv="#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 uVP;\nvoid main(){gl_Position=uVP*vec4(p,1.0);}\n";
static const char*qf="#version 330 core\nout vec4 c;\nvoid main(){c=vec4(0.30,0.26,0.20,1.0);}\n";
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("d",0,0,VW,VH,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
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
    /* rampa pura: hondo en -X, orilla a +X */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){float u=(float)i/(S-1); terr[j*S+i]=-12.0f+15.0f*u;}
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);
    /* camara CENITAL: el eje X del mundo es el X de pantalla, sin perspectiva rara */
    G3DCamera*cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(0,150,0.1f); cam->fov=60; cam->near_plane=1; cam->far_plane=600;
    cam->aspect_ratio=(float)VW/VH;
    g3d_camera_look_at_impl(cam,vec3_make(0,0,0),vec3_make(0,0,-1));
    g3d_camera_update(cam);
    GLuint p=glCreateProgram(),vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&qv,0);glCompileShader(vs);glShaderSource(fs,1,&qf,0);glCompileShader(fs);
    glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
    int G=S-1; float*vt=malloc((size_t)G*G*18*sizeof(float)); int nv=0;
    for(int j=0;j<G;j++)for(int i=0;i<G;i++){
        float x0=((float)i/G-0.5f)*WS,x1=((float)(i+1)/G-0.5f)*WS;
        float z0=((float)j/G-0.5f)*WS,z1=((float)(j+1)/G-0.5f)*WS;
        float q[18]={x0,terr[j*S+i],z0, x1,terr[j*S+i+1],z0, x1,terr[(j+1)*S+i+1],z1,
                     x0,terr[j*S+i],z0, x1,terr[(j+1)*S+i+1],z1, x0,terr[(j+1)*S+i],z1};
        for(int k=0;k<18;k++) vt[nv*3+k]=q[k];
        nv+=6;
    }
    GLuint va,vb; glGenVertexArrays(1,&va); glGenBuffers(1,&vb);
    glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER,vb);
    glBufferData(GL_ARRAY_BUFFER,(size_t)nv*3*sizeof(float),vt,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    Mat4 vp=mat4_multiply(g3d_camera_get_projection(cam),g3d_camera_get_view(cam));
    g3d_water_render_set_surf(1.2f, 2.0f, 0.5f, 2.0f);
    g3d_water_render_set_surf_wave(0.9f, 0.0f);
    g3d_water_render_set_caustics(0.0f);   /* fuera ruido: solo espuma */
    unsigned char*px=malloc(VW*VH*4);
    glViewport(0,0,VW,VH);
    printf("centroide en X de la espuma (pantalla: -X izquierda = mar, +X derecha = orilla)\n");
    for(int fr=0;fr<6;fr++){
        glClearColor(0.2f,0.3f,0.45f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
        glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,nv); glBindVertexArray(0);
        g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
        g3d_water_render(cam,0);
        glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
        static double prev[VW]; static int have=0;
        double cur[VW];
        for(int x=0;x<VW;x++){
            double a=0; for(int y=VH/3;y<2*VH/3;y++){
                int i2=(y*VW+x)*4; double v=(px[i2]+px[i2+1]+px[i2+2])/3.0;
                a += (v>170)? v : 0.0; }
            cur[x]=a;
        }
        if(have){
            int best=0; double bc=-1e30;
            for(int sh=-40; sh<=40; sh++){
                double c=0; int n=0;
                for(int x=60;x<VW-60;x++){ int y2=x+sh; if(y2<0||y2>=VW) continue;
                    c += prev[x]*cur[y2]; n++; }
                if(n){ c/=n; if(c>bc){bc=c;best=sh;} }
            }
            printf("  paso %d: desplazamiento %+d px  ->  %s\n", fr, best,
                   best>1 ? "HACIA LA ORILLA" : (best<-1 ? "mar adentro" : "sin movimiento claro"));
        }
        for(int x=0;x<VW;x++) prev[x]=cur[x];
        have=1;
        SDL_Delay(150);
    }
    return 0;
}
