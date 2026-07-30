/* Lagos y rios MANUALES deben acabar en el campo unificado, no en mallas aparte. */
#include "libmod_3d_water_field.h"
#include <stdio.h>
#include <math.h>
#define S 129
#define WS 128.0f
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:"");
    if(!ok)fails++;
}
int main(void){
    /* cuenca en el centro, terreno a 10 alrededor */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float x=(float)i-64, z=(float)j-64;
        float r=sqrtf(x*x+z*z);
        terr[j*S+i] = (r<20.0f) ? (10.0f - 12.0f*(1.0f-r/20.0f)) : 10.0f;
    }
    g3d_waterfield_init(terr,S,WS);
    g3d_waterfield_set_evaporation(0.0f);
    char buf[160];

    printf("1. llenar una cuenca (lago manual)\n");
    int cells=g3d_waterfield_fill_basin(0.0f,0.0f,5.0f,0.0f);
    snprintf(buf,sizeof buf,"%d celdas",cells);
    check("la cuenca se llena",cells>50,buf);
    float lvl=g3d_waterfield_level_at(0.0f,0.0f);
    snprintf(buf,sizeof buf,"nivel=%.2f (pedido 5.00)",lvl);
    check("la superficie queda al nivel pedido",fabsf(lvl-5.0f)<0.01f,buf);

    /* el terreno fuera de la cuenca esta a 10: el lago NO debe desbordarse */
    float out=g3d_waterfield_depth_at(WS*0.45f,0.0f);
    snprintf(buf,sizeof buf,"profundidad fuera=%.3f",out);
    check("no inunda el terreno alto de alrededor",out<0.01f,buf);

    printf("2. el radio maximo acota el llenado\n");
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    int few=g3d_waterfield_fill_basin(0.0f,0.0f,5.0f,4.0f);
    snprintf(buf,sizeof buf,"%d celdas con radio 4 (antes %d sin limite)",few,cells);
    check("el radio limita el area",few>0&&few<cells,buf);

    printf("3. cauce de rio manual\n");
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr,S,WS);
    /* rio que desciende de x=-50 a x=+50 por encima del terreno (10) */
    float pts[]={ -50.0f, 12.0f, -30.0f,
                    0.0f, 11.0f, -30.0f,
                   50.0f, 10.5f, -30.0f };
    int rc=g3d_waterfield_add_channel(pts,3,6.0f);
    snprintf(buf,sizeof buf,"%d celdas",rc);
    check("el cauce deposita agua",rc>20,buf);
    float mid=g3d_waterfield_level_at(0.0f,-30.0f);
    snprintf(buf,sizeof buf,"nivel=%.2f (pedido ~11)",mid);
    check("la superficie sigue la altura pedida",fabsf(mid-11.0f)<0.3f,buf);
    float off=g3d_waterfield_depth_at(0.0f,0.0f);
    snprintf(buf,sizeof buf,"profundidad a 30u del cauce=%.3f",off);
    check("no moja fuera de su anchura",off<0.01f,buf);

    /* El agua de AUTOR se queda donde la pones: no cede su propio volumen, asi
       que ni se seca ni se desborda. Lo que si fluye es lo que se le eche
       ENCIMA -- la simulacion sigue viva ahi, solo que no puede vaciar lo que el
       autor coloco. */
    printf("4. el cauce colocado se queda\n");
    float placed=g3d_waterfield_depth_at(0.0f,-30.0f);
    g3d_waterfield_settle(120.0f);
    float after=g3d_waterfield_depth_at(0.0f,-30.0f);
    snprintf(buf,sizeof buf,"%.3f -> %.3f tras 2 min",placed,after);
    check("no se escurre",after>placed*0.9f,buf);

    printf("5. y un manantial fuerte encima no lo desborda\n");
    /* El excedente baja POR el cauce, no de lado: se mide aguas abajo, donde
       tiene que subir por encima de la profundidad colocada. */
    const float *dd=g3d_waterfield_depth_array();
    int NN=g3d_waterfield_side()*g3d_waterfield_side();
    double v0=0; for(int i=0;i<NN;i++) v0+=dd[i];
    g3d_waterfield_add_spring(-40.0f,-30.0f,4.0f);
    g3d_waterfield_settle(25.0f);
    double v1=0; for(int i=0;i<NN;i++) v1+=dd[i];
    g3d_waterfield_settle(180.0f);
    double v2=0; for(int i=0;i<NN;i++) v2+=dd[i];
    snprintf(buf,sizeof buf,"volumen %.0f -> %.0f -> %.0f (3 min mas)",v0,v1,v2);
    check("alcanza equilibrio en vez de crecer",v2<=v1*1.05+2.0,buf);
    check("y el cauce sigue entero",v2>v0*0.9,buf);

    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
