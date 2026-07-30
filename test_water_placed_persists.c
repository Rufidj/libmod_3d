/* Un lago colocado a mano NO debe evaporarse ni escurrirse.
   Al enrutar los lagos al campo quedaron sometidos a la simulacion, y con la
   evaporacion por defecto y sin nada que los alimente desaparecian en un par de
   minutos: el editor mostraba "Lagos: 10" y ni una gota de agua. El agua
   colocada es una condicion de contorno, como el mar. */
#include "libmod_3d_water_field.h"
#include <stdio.h>
#include <math.h>
#define S 161
#define WS 400.0f
static float terr[S*S];
static int fails=0;
static void check(const char*w,int ok,const char*d){
    printf("  [%s] %s%s%s\n",ok?"PASS":"FAIL",w,d&&*d?" -- ":"",d?d:""); if(!ok)fails++; }
int main(void){
    /* cuenca poco profunda entre relieve, como un hoyo del editor */
    for(int j=0;j<S;j++)for(int i=0;i<S;i++){
        float x=(float)i-80,z=(float)j-80,r=sqrtf(x*x+z*z);
        terr[j*S+i]=(r<28.0f)?(10.0f-9.0f*(1.0f-r/28.0f)):10.0f;
    }
    g3d_waterfield_init(terr,S,WS);
    char b[160];
    printf("1. lago colocado\n");
    int cells=g3d_waterfield_fill_basin(0,0,7.0f,0.0f);
    snprintf(b,sizeof b,"%d celdas",cells);
    check("se llena",cells>50,b);
    float l0=g3d_waterfield_level_at(0,0);
    snprintf(b,sizeof b,"nivel=%.2f",l0);
    check("al nivel pedido",fabsf(l0-7.0f)<0.05f,b);

    printf("2. sobrevive a la simulacion (evaporacion por defecto)\n");
    g3d_waterfield_settle(180.0f);   /* tres minutos de sim */
    float l1=g3d_waterfield_level_at(0,0);
    snprintf(b,sizeof b,"nivel=%.2f tras 180s",l1);
    check("el lago SIGUE ahi",l1>G3D_NO_WATER_TEST&&fabsf(l1-7.0f)<0.3f,b);

    printf("3. al soltar las retenciones si se va\n");
    g3d_waterfield_clear_holds();
    g3d_waterfield_settle(300.0f);
    float l2=g3d_waterfield_level_at(0,0);
    snprintf(b,sizeof b,"nivel=%.2f",l2<G3D_NO_WATER_TEST?-99.0f:l2);
    check("sin retencion se evapora",l2<G3D_NO_WATER_TEST||l2<3.0f,b);

    printf("\n%s (%d fallo%s)\n",fails?"FALLOS":"TODO OK",fails,fails==1?"":"s");
    return fails?1:0;
}
