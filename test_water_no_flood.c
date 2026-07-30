/* EL AGUA NO PUEDE CRECER SIN LIMITE.
   Al hacer que el agua colocada se repusiera cada paso se convirtio en una fuente
   inagotable: el lago rebosaba, la reposicion sustituia lo que se habia ido, y el
   agua de aguas abajo crecia sin freno hasta inundar el mapa. Y por otro lado el
   editor reconstruye su agua en cada edicion, asi que un manantial por
   reconstruccion se acumulaba igual. Ambas cosas se miden aqui. */
#include "libmod_3d_water_field.h"
#include <stdio.h>
#include <math.h>
#define S 161
#define WS 400.0f
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
static double volume(void){
    const float*d=g3d_waterfield_depth_array(); double v=0;
    for(int i=0;i<S*S;i++) v+=d[i]; return v;
}
int main(void){
    char b[200];
    /* cuenca en una ladera: el lago rebosa por el lado bajo */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float u=(float)i/(S-1);
        float h=30.0f-25.0f*u;
        float x=(float)i-60,z=(float)j-80,r=sqrtf(x*x+z*z);
        if(r<22.0f) h-=8.0f*(1.0f-r/22.0f);
        terr[j*S+i]=h;
    }
    g3d_waterfield_init(terr,S,WS);

    printf("1. un lago colocado en una ladera no inunda el mapa\n");
    g3d_waterfield_fill_basin(-50.0f, 0.0f, 16.0f, 45.0f);
    g3d_waterfield_settle(60.0f);
    double v1=volume();
    g3d_waterfield_settle(240.0f);      /* cuatro minutos mas */
    double v2=volume();
    snprintf(b,sizeof b,"volumen %.0f -> %.0f tras 4 min mas",v1,v2);
    check("el volumen no crece sin control", v2 <= v1*1.10 + 5.0, b);
    check("y el lago sigue existiendo", v2 > 1.0, "");

    /* EL CASO DEL MODO JUEGO. El editor reconstruye su agua a cada edicion, asi
       que una acumulacion lenta pasa desapercibida; el motor la deja correr
       minutos y se desborda. Un rio colocado tiene manantial en la cabecera, asi
       que hay entrada constante: tiene que existir un equilibrio. */
    printf("2. un rio alimentado alcanza equilibrio, no crece sin fin\n");
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    {
        /* Anchura y trazado como los que genera el editor de verdad. */
        float pts[]={ -180.0f, 30.2f, -170.0f,  -60.0f, 22.7f, -120.0f,
                        20.0f, 17.0f,  -60.0f };
        g3d_waterfield_add_channel(pts,3,11.4f);
    }
    g3d_waterfield_settle(120.0f);
    double r1=volume();
    g3d_waterfield_settle(300.0f);      /* cinco minutos mas de partida */
    double r2=volume();
    snprintf(b,sizeof b,"volumen %.0f -> %.0f tras 5 min mas",r1,r2);
    check("el rio no crece", r2 <= r1*1.05 + 5.0, b);
    /* Y sobre todo: NO se seca. Un rio colocado se queda donde lo pusiste. */
    snprintf(b,sizeof b,"volumen final %.0f (inicial %.0f)",r2,r1);
    check("el rio sigue ahi tras 7 minutos", r2 > r1*0.7, b);

    printf("3. reconstruir el agua no acumula manantiales\n");
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    for(int rebuild=0; rebuild<12; rebuild++){
        g3d_waterfield_clear_springs_tagged(1);       /* como hace g3d_flow_clear */
        g3d_waterfield_add_spring_tagged(-20.0f,0.0f,3.0f,1);
    }
    snprintf(b,sizeof b,"%d manantiales tras 12 reconstrucciones",
             g3d_waterfield_spring_count());
    check("solo queda uno", g3d_waterfield_spring_count()==1, b);

    printf("4. las etiquetas no se pisan entre si\n");
    g3d_waterfield_add_spring(50.0f, 0.0f, 1.0f);     /* del usuario, etiqueta 0 */
    g3d_waterfield_clear_springs_tagged(1);
    snprintf(b,sizeof b,"%d manantiales",g3d_waterfield_spring_count());
    check("quitar los de cascada respeta los del usuario",
          g3d_waterfield_spring_count()==1, b);

    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
