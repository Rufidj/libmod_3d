/* El agua no se queda colgada sobre la roca seca.
 *
 * Una celda seca declara el nivel del agua que tiene al lado, para que la lamina
 * llegue plana hasta la orilla en vez de hundirse en el talud. Donde eso se
 * rompe es en un canon: la pared seca tiene agua a los dos lados y a alturas muy
 * distintas -- el lago abajo, el arroyo de la meseta arriba -- y el filtrado
 * bilineal entre las dos levanta laminas enormes plantadas en el aire sobre la
 * pared. En la vista salen como sabanas planas y semitransparentes que tapan
 * medio encuadre.
 *
 * La regla que las quita es leer la CELDA en vez de su interpolacion: donde la
 * simulacion dice que no hay agua, no se dibuja. Aqui se monta el canon y se
 * cuenta cuanta agua queda sobre la mitad del encuadre donde solo hay canon,
 * por encima del horizonte, que es donde no puede haber ninguna.
 */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>

#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_camera.h"

#define S  161
#define WS 400.0f
#define VW 1280
#define VH 720

static int fails = 0;
static float terr[S * S];
static unsigned char px[VW * VH * 4];

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) fails++;
}

static int screen_row(G3DCamera *cam, float x, float y, float z) {
    Mat4 vp = mat4_multiply(g3d_camera_get_projection(cam), g3d_camera_get_view(cam));
    float w  = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
    float cy = vp.m[1] * x + vp.m[5] * y + vp.m[9]  * z + vp.m[13];
    if (fabsf(w) < 1e-6f) return -1;
    return (int)((cy / w * 0.5f + 0.5f) * VH);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("sin SDL: %s\n", SDL_GetError()); return 77; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("muro", 0, 0, VW, VH,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { printf("sin ventana: %s\n", SDL_GetError()); SDL_Quit(); return 77; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("sin contexto: %s\n", SDL_GetError()); SDL_Quit(); return 77; }

    GLuint fbo = 0, colour = 0, depth = 0;
    glGenTextures(1, &colour);
    glBindTexture(GL_TEXTURE_2D, colour);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VW, VH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, VW, VH);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("destino incompleto\n"); return 1;
    }
    glViewport(0, 0, VW, VH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    /* Canon de paredes casi verticales con meseta encima, a escala de editor:
       400 unidades, celda de 2.5. El mar llena el canon, el manantial moja la
       meseta, y entre los dos queda la pared seca. */
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (-WS * 0.5f) + (float)i * (WS / (S - 1));
            float z = (-WS * 0.5f) + (float)j * (WS / (S - 1));
            float h = 30.0f - 0.02f * z;
            if (fabsf(x) < 28.0f)      h = 0.0f;
            else if (fabsf(x) < 33.0f) h = 30.0f * ((fabsf(x) - 28.0f) / 5.0f);
            terr[j * S + i] = h;
        }
    g3d_waterfield_init(terr, S, WS);
    g3d_waterfield_set_evaporation(0.01f);
    g3d_waterfield_set_sea_level(8.0f);
    g3d_waterfield_add_spring(60.0f, 60.0f, 20.0f);
    g3d_waterfield_settle(90.0f);

    char buf[200];
    float lago = g3d_waterfield_level_at(0.0f, 60.0f);
    float meseta = g3d_waterfield_level_at(45.0f, 60.0f);
    snprintf(buf, sizeof buf, "lago=%.1f  meseta=%.1f", lago, meseta);
    check("hay dos aguas a alturas muy distintas", meseta - lago > 12.0f, buf);

    G3DCamera *cam = g3d_camera_impl_create(0);
    if (!cam) { printf("sin camara\n"); return 1; }
    cam->fov = 60.0f; cam->near_plane = 0.1f; cam->far_plane = 2000.0f;
    cam->aspect_ratio = (float)VW / (float)VH;
    cam->position = vec3_make(0.0f, 14.0f, 90.0f);
    g3d_camera_look_at_impl(cam, vec3_make(24.0f, 18.0f, 45.0f), vec3_make(0, 1, 0));
    g3d_camera_update(cam);

    for (int f = 0; f < 4; f++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
    }
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* Por encima del HORIZONTE (la altura de la camara vista de lejos) solo
       puede haber la lamina lejana de la meseta, que cae en la mitad derecha del
       encuadre. La mitad izquierda es canon: alli arriba no hay agua ninguna, y
       lo que aparezca es una sabana colgada sobre la pared. */
    int horiz = screen_row(cam, 0.0f, cam->position.y, -900.0f);
    snprintf(buf, sizeof buf, "fila %d de %d", horiz, VH);
    check("el horizonte cae dentro del encuadre", horiz > 40 && horiz < VH - 40, buf);

    long colgada = 0, zona = 0;
    for (int y = horiz + 12; y < VH; y++)
        for (int x = 0; x < VW / 2; x++) {
            const unsigned char *p = &px[(y * VW + x) * 4];
            zona++;
            if (p[0] + p[1] + p[2] > 12) colgada++;
        }
    double frac = zona ? (double)colgada / (double)zona : 0.0;
    snprintf(buf, sizeof buf, "%.1f%% de la zona (%ld px)", frac * 100.0, colgada);
    check("no hay laminas colgadas sobre el canon", frac < 0.05, buf);

    /* Y no se descarta de mas: el lago sigue llenando la parte baja. */
    long lago_px = 0;
    for (int y = 0; y < horiz - 12 && y < VH; y++)
        for (int x = 0; x < VW; x++) {
            const unsigned char *p = &px[(y * VW + x) * 4];
            if (p[0] + p[1] + p[2] > 12) lago_px++;
        }
    snprintf(buf, sizeof buf, "%ld px", lago_px);
    check("el agua que si existe se sigue viendo", lago_px > 100000, buf);

    g3d_water_render_shutdown();
    g3d_waterfield_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf(fails ? "\n%d FALLOS\n" : "\nTODO CORRECTO (%d fallos)\n", fails);
    return fails ? 1 : 0;
}
