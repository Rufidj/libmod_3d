/* Olas de playa: rompientes en el bajio y swash que sube por la arena.
   El swash NO puede vivir en el shader de superficie: sobre arena seca no hay
   agua y el fragmento se descarta. Va en un pase de pantalla, asi que hay que
   comprobar que de verdad pinta por encima de la linea de agua. */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_camera.h"
#define S 161
#define WS 300.0f
#define VW 640
#define VH 400
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
static const char*qv="#version 330 core\nlayout(location=0) in vec3 p;\nuniform mat4 uVP;\nvoid main(){gl_Position=uVP*vec4(p,1.0);}\n";
static const char*qf="#version 330 core\nout vec4 c;\nvoid main(){c=vec4(0.82,0.74,0.55,1.0);}\n";  /* arena */
int main(void){
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("b",0,0,VW,VH,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
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

    /* playa: pendiente suave de -10 (mar) a +6 (duna) a lo largo de X */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1);
        terr[j*S+i] = -10.0f + 16.0f*u + 0.25f*sinf(j*0.21f);
    }
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);
    char b[200];
    printf("1. la playa tiene bajio\n");
    float d_far=g3d_waterfield_depth_at(-120.0f,0.0f);
    float d_shore=g3d_waterfield_depth_at(-8.0f,0.0f);
    snprintf(b,sizeof b,"lejos=%.2f  cerca de la orilla=%.2f",d_far,d_shore);
    check("la profundidad decrece hacia la orilla", d_far>d_shore+1.0f, b);

    /* la arena (geometria opaca) */
    GLuint p=glCreateProgram(),vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs,1,&qv,0); glCompileShader(vs); glShaderSource(fs,1,&qf,0); glCompileShader(fs);
    glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    int G=S-1; float*vtx=malloc((size_t)G*G*6*3*sizeof(float)); int nv=0;
    for(int j=0;j<G;j++)for(int i=0;i<G;i++){
        float x0=((float)i/G-0.5f)*WS, x1=((float)(i+1)/G-0.5f)*WS;
        float z0=((float)j/G-0.5f)*WS, z1=((float)(j+1)/G-0.5f)*WS;
        float y00=terr[j*S+i],y10=terr[j*S+i+1],y01=terr[(j+1)*S+i],y11=terr[(j+1)*S+i+1];
        float q[18]={x0,y00,z0, x1,y10,z0, x1,y11,z1, x0,y00,z0, x1,y11,z1, x0,y01,z1};
        for(int k=0;k<18;k++) vtx[nv*3+k/3*0+k]=q[k];
        nv+=6;
    }
    GLuint va,vb; glGenVertexArrays(1,&va); glGenBuffers(1,&vb);
    glBindVertexArray(va); glBindBuffer(GL_ARRAY_BUFFER,vb);
    glBufferData(GL_ARRAY_BUFFER,(size_t)nv*3*sizeof(float),vtx,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);

    G3DCamera*cam=g3d_camera_impl_create(0);
    cam->position=vec3_make(-30.0f,22.0f,70.0f); cam->fov=60; cam->near_plane=0.1f;
    cam->far_plane=800; cam->aspect_ratio=(float)VW/VH;
    g3d_camera_look_at_impl(cam,vec3_make(10.0f,0.0f,0.0f),vec3_make(0,1,0));
    g3d_camera_update(cam);
    glViewport(0,0,VW,VH);
    Mat4 vp=mat4_multiply(g3d_camera_get_projection(cam),g3d_camera_get_view(cam));
    unsigned char*px=malloc(VW*VH*4);
    unsigned char*ref=malloc(VW*VH*4);
    long changed=0, sandpx=0;
    for(int pass=0;pass<2;pass++){
        g3d_water_render_set_surf(pass==0?0.0f:1.0f, 9.0f, 0.16f, 3.0f);
        glClearColor(0.45f,0.62f,0.85f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
        glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,nv); glBindVertexArray(0);
        g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
        g3d_water_render(cam,0);
        glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
        /* Se comparan LOS MISMOS pixeles: los que sin surf salieron arena.
           Contar sobre toda la pantalla no sirve, lo dominan los 200k pixeles de
           agua y el swash se pierde en el ruido. */
        if(pass==0){ memcpy(ref,px,(size_t)VW*VH*4);
            for(int i=0;i<VW*VH;i++){
                int r=ref[i*4],g=ref[i*4+1],bb=ref[i*4+2];
                if(r>150 && g>120 && bb<170 && r>bb+30) sandpx++;
            }
        } else {
            for(int i=0;i<VW*VH;i++){
                int r=ref[i*4],g=ref[i*4+1],bb=ref[i*4+2];
                if(!(r>150 && g>120 && bb<170 && r>bb+30)) continue;   /* era arena */
                int dr=abs(r-px[i*4]),dg=abs(g-px[i*4+1]),db=abs(bb-px[i*4+2]);
                if(dr+dg+db>30) changed++;
            }
        }
        if(pass==1){FILE*ff=fopen("/tmp/beach.ppm","wb"); fprintf(ff,"P6\n%d %d\n255\n",VW,VH);
            for(int y=VH-1;y>=0;y--)for(int x=0;x<VW;x++)fwrite(&px[(y*VW+x)*4],1,3,ff); fclose(ff);}
    }
    printf("2. el swash moja la arena\n");
    snprintf(b,sizeof b,"%ld de %ld pixeles de arena mojados (%.1f%%)",
             changed,sandpx,sandpx?100.0*changed/sandpx:0.0);
    check("el swash moja la arena", changed > sandpx/20 && changed > 200, b);
    check("pero no la inunda entera", changed < sandpx*9/10, b);
    int e=0; while(glGetError()!=GL_NO_ERROR) e++;
    check("sin errores GL", e==0, "");
    /* EL CASO QUE LO ROMPIO. En terreno casi llano cerca del nivel del mar,
       TODO punto del mapa esta dentro de la altura de subida, asi que un swash
       que solo mire la altura pinta el mapa entero de blanco una vez por ola.
       Tiene que acotarse por DISTANCIA al agua, no solo por altura. */
    printf("3. terreno llano: el swash no puede blanquear el mapa\n");
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1);
        terr[j*S+i] = (u<0.25f) ? (-6.0f+22.0f*u) : (0.6f+0.15f*sinf(i*0.3f)*cosf(j*0.3f));
    }
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);
    /* El swash OSCILA, asi que un solo fotograma mide una fase al azar y no dice
       nada. Se acelera el ciclo y se toma el MAXIMO sobre varios fotogramas, que
       es el caso que se ve mal: el instante de maxima subida. */
    long flat_changed=0, flat_sand=0;
    for(int pass=0;pass<2;pass++){
        g3d_water_render_set_surf(pass==0?0.0f:1.0f, 9.0f, 60.0f, 3.0f);
        int frames = (pass==0) ? 1 : 40;
        for(int fr=0; fr<frames; fr++){
        glClearColor(0.45f,0.62f,0.85f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
        glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,nv); glBindVertexArray(0);
        g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
        g3d_water_render(cam,0);
        glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
        if(pass==0){ memcpy(ref,px,(size_t)VW*VH*4);
            for(int i=0;i<VW*VH;i++){int r=ref[i*4],g=ref[i*4+1],bb=ref[i*4+2];
                if(r>150&&g>120&&bb<170&&r>bb+30) flat_sand++; }
        } else {
            long n=0;
            for(int i=0;i<VW*VH;i++){int r=ref[i*4],g=ref[i*4+1],bb=ref[i*4+2];
                if(!(r>150&&g>120&&bb<170&&r>bb+30)) continue;
                if(abs(r-px[i*4])+abs(g-px[i*4+1])+abs(bb-px[i*4+2])>30) n++; }
            if(n>flat_changed) flat_changed=n;
        }
        SDL_Delay(3);
        }
    }
    snprintf(b,sizeof b,"maximo %ld de %ld pixeles de llano (%.1f%%)",
             flat_changed,flat_sand,flat_sand?100.0*flat_changed/flat_sand:0.0);
    check("el llano lejano NO se blanquea", flat_sand==0 || flat_changed < flat_sand/4, b);

    /* EL CASO REAL. Un BAJIO ancho y somero (la plataforma de una playa) no
       puede salir blanco entero. La espuma de rompiente se compara con la
       profundidad, y como la amplitud por shoaling crece sin freno al bajar
       esta, el cociente supera el umbral en TODO el bajio y lo pinta de blanco.
       Se mide el maximo a lo largo del ciclo, que es cuando peor se ve. */
    printf("4. un bajio ancho no puede salir blanco entero\n");
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1);
        terr[j*S+i] = (u<0.15f) ? (-9.0f+40.0f*u)      /* talud a la mar */
                                : (-0.35f+0.10f*sinf(i*0.25f)*cosf(j*0.25f));
    }
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(5.0f);
    float dshelf=g3d_waterfield_depth_at(60.0f,0.0f);
    snprintf(b,sizeof b,"profundidad del bajio=%.2f",dshelf);
    check("hay bajio somero", dshelf>0.05f && dshelf<1.2f, b);

    long whitest=0;
    g3d_water_render_set_surf(1.0f, 9.0f, 60.0f, 3.0f);
    for(int fr=0; fr<40; fr++){
        glClearColor(0.45f,0.62f,0.85f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
        glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,nv); glBindVertexArray(0);
        g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
        g3d_water_render(cam,0);
        glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
        long n=0;
        for(int i=0;i<VW*VH;i++)
            if(px[i*4]>205 && px[i*4+1]>205 && px[i*4+2]>205) n++;
        if(n>whitest) whitest=n;
        SDL_Delay(3);
    }
    snprintf(b,sizeof b,"maximo %ld de %d pixeles blancos (%.1f%%)",
             whitest,VW*VH,100.0*whitest/(VW*VH));
    check("el bajio no se vuelve una sabana blanca", whitest < (long)VW*VH/4, b);

    /* LAS OLAS TIENEN QUE IR HACIA LA ORILLA. Con la fase +profundidad*k la
       cresta se mantiene donde la fase es constante, asi que al correr el tiempo
       la profundidad AUMENTA y el oleaje se marcha mar adentro. Se mide de
       verdad: se coge una linea de la zona de rompientes en dos instantes y se
       busca el desplazamiento que mejor correlaciona. */
    printf("5. las olas viajan hacia la orilla\n");
    {
        /* playa simple: hondo en -X, orilla hacia +X, sin curvatura */
        for(int j=0;j<S;j++)for(int i=0;i<S;i++){
            float u=(float)i/(S-1);
            terr[j*S+i] = -12.0f + 15.0f*u;
        }
        g3d_waterfield_shutdown();
        g3d_waterfield_init(terr,S,WS);
        g3d_waterfield_set_sea_level(0.0f);
        g3d_waterfield_settle(4.0f);
        g3d_water_render_set_surf(1.0f, 2.5f, 0.30f, 2.0f);
        g3d_water_render_set_surf_wave(0.8f, 0.0f);

        int row = VH/2, W2 = VW;
        static float a[VW], bb2[VW];
        for(int pass=0; pass<2; pass++){
            glClearColor(0.45f,0.62f,0.85f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
            glUseProgram(p); glUniformMatrix4fv(glGetUniformLocation(p,"uVP"),1,GL_FALSE,vp.m);
            glBindVertexArray(va); glDrawArrays(GL_TRIANGLES,0,nv); glBindVertexArray(0);
            g3d_renderer_capture_scene(); g3d_renderer_capture_depth();
            g3d_water_render(cam,0);
            glReadPixels(0,0,VW,VH,GL_RGBA,GL_UNSIGNED_BYTE,px);
            for(int x=0;x<W2;x++){
                int i2=(row*VW+x)*4;
                float v=(px[i2]+px[i2+1]+px[i2+2])/3.0f;
                if(pass==0) a[x]=v; else bb2[x]=v;
            }
            if(pass==0) SDL_Delay(1400);
        }
        /* correlacion: que desplazamiento hace que el segundo encaje con el primero */
        int best=0; double bestc=-1e30;
        for(int sh=-90; sh<=90; sh++){
            double c=0; int cnt=0;
            for(int x=40;x<W2-40;x++){
                int y2=x+sh; if(y2<0||y2>=W2) continue;
                c += (double)a[x]*bb2[y2]; cnt++;
            }
            if(cnt>0){ c/=cnt; if(c>bestc){bestc=c;best=sh;} }
        }
        /* Si el patron se desplaza D pixeles a la derecha entre fotogramas,
           bb2[x] = a[x-D], luego bb2[x+sh] encaja con a[x] en sh = +D. Positivo
           = hacia +X = hacia la orilla en esta escena. */
        snprintf(b,sizeof b,"desplazamiento %+d px (positivo = hacia la orilla)",best);
        check("el oleaje avanza hacia la orilla", best > 2, b);
    }

    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
