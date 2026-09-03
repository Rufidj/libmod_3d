/* La cascada la dibuja LA PROPIA MALLA del agua, no cortinas aparte.
 *
 * Antes, una caida eran quads semitransparentes colgados sobre el acantilado:
 * una lamina por celda, unas setenta en una escena asentada, cada una mezclando
 * sobre la de detras. Ahora la superficie teselada baja por el acantilado ella
 * misma. Aqui se comprueba lo que eso tiene que producir:
 *   1. con las cortinas APAGADAS el agua que cae se sigue viendo;
 *   2. la espuma crece HACIA ABAJO a lo largo de toda la caida (es lo unico que
 *      distingue agua cayendo de una textura que se mueve, y solo sale bien si
 *      la caida se mide entera y no celda a celda);
 *   3. quitando la caida (pendiente imposible) esa senal desaparece -- control
 *      negativo, para que la prueba no pase por el mero hecho de haber agua;
 *   4. un lago entre colinas NO se pinta de blanco: una pendiente pronunciada
 *      del TERRENO no es una caida, y confundirlas fue el error que dejo medio
 *      mapa cubierto de cortinas la primera vez.
 */
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "libmod_3d_glcaps.h"
#include "libmod_3d_water_field.h"
#include "libmod_3d_water_render.h"
#include "libmod_3d_water_falls.h"
#include "libmod_3d_camera.h"

#define S  129
#define WS 128.0f
#define VW 640
#define VH 480

static int fails = 0;
static unsigned char px[VW * VH * 4];

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) fails++;
}

static void APIENTRY dbg(GLenum src, GLenum type, GLuint id, GLenum sev,
                         GLsizei len, const GLchar *msg, const void *user) {
    (void)src; (void)id; (void)len; (void)user;
    if (type == GL_DEBUG_TYPE_ERROR || sev == GL_DEBUG_SEVERITY_HIGH)
        printf("        GL DEBUG: %s\n", msg);
}

/* Brillo medio del agua dibujada dentro de un rectangulo de pantalla, y cuantos
   pixeles ha cubierto. El fondo es negro, asi que "hay agua" es "no es negro". */
static double band_mean(int x0, int y0, int x1, int y1, long *count) {
    long n = 0; double sum = 0.0;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= VH) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= VW) continue;
            const unsigned char *p = &px[(y * VW + x) * 4];
            int lum = p[0] + p[1] + p[2];
            if (lum <= 12) continue;                 /* fondo */
            sum += lum / 3.0; n++;
        }
    }
    if (count) *count = n;
    return n ? sum / (double)n : 0.0;
}

/* Fraccion de pixeles casi blancos: lo que delata que el agua se ha lavado. */
static double white_fraction(void) {
    long white = 0, drawn = 0;
    for (int i = 0; i < VW * VH; i++) {
        const unsigned char *p = &px[i * 4];
        if (p[0] + p[1] + p[2] <= 12) continue;
        drawn++;
        if (p[0] > 200 && p[1] > 200 && p[2] > 200) white++;
    }
    return drawn ? (double)white / (double)drawn : 0.0;
}

static void draw(G3DCamera *cam) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

/* Fila de pantalla de un punto del mundo. glReadPixels devuelve la imagen con el
   origen ABAJO, asi que la fila que sale de aqui ya esta en ese sistema. */
static int screen_row(G3DCamera *cam, float x, float y, float z) {
    Mat4 vp = mat4_multiply(g3d_camera_get_projection(cam), g3d_camera_get_view(cam));
    float w = vp.m[3] * x + vp.m[7] * y + vp.m[11] * z + vp.m[15];
    float cy = vp.m[1] * x + vp.m[5] * y + vp.m[9] * z + vp.m[13];
    if (fabsf(w) < 1e-6f) return -1;
    return (int)((cy / w * 0.5f + 0.5f) * VH);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("falls", 0, 0, VW, VH,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { printf("no window: %s\n", SDL_GetError()); SDL_Quit(); return 77; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("no GL context: %s\n", SDL_GetError()); SDL_Quit(); return 77; }
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(dbg, NULL);

    GLuint fbo = 0, colour = 0, depth = 0;
    glGenTextures(1, &colour);
    glBindTexture(GL_TEXTURE_2D, colour);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VW, VH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, VW, VH);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("offscreen target incomplete\n"); return 1;
    }
    glViewport(0, 0, VW, VH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    char buf[200];

    /* --- la escena: meseta inclinada, un escalon, y suelo -------------------
       La meseta BAJA hacia el borde a proposito. Sobre una meseta plana el
       manantial se reparte en circulo y se escurre por el borde del mapa mas
       cercano antes de llegar al escalon, y no cae nada: ese error ya ha
       costado dos pruebas. */
    static float cliff[S * S];
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float u = (float)i / (float)(S - 1);
            cliff[j * S + i] = (u < 0.45f) ? (24.0f - 8.0f * u)
                             : (u < 0.48f) ? (20.4f - 18.4f * ((u - 0.45f) / 0.03f))
                                           : 2.0f;
        }
    /* El borde cae entre u=0.45 y u=0.48, o sea x = -6.4 .. -2.56. */
    const float FALL_X = -4.5f, FALL_TOP = 20.4f, FALL_BOT = 2.0f;

    g3d_waterfield_init(cliff, S, WS);
    g3d_waterfield_set_evaporation(0.01f);
    g3d_waterfield_add_spring(-WS * 0.35f, 0.0f, 8.0f);
    g3d_waterfield_settle(60.0f);

    G3DCamera *cam = g3d_camera_impl_create(0);
    if (!cam) { printf("no camera\n"); return 1; }
    cam->position = vec3_make(28.0f, 12.0f, 0.0f);
    cam->fov = 60.0f;
    cam->near_plane = 0.1f;
    cam->far_plane = 500.0f;
    cam->aspect_ratio = (float)VW / (float)VH;
    g3d_camera_look_at_impl(cam, vec3_make(FALL_X, 11.0f, 0.0f), vec3_make(0, 1, 0));
    g3d_camera_update(cam);

    int row_top = screen_row(cam, FALL_X, FALL_TOP, 0.0f);
    int row_bot = screen_row(cam, FALL_X, FALL_BOT, 0.0f);
    if (row_top < row_bot) { int t = row_top; row_top = row_bot; row_bot = t; }
    int span = row_top - row_bot;
    if (span < 30) {
        printf("la caida no cabe en el encuadre (%d filas)\n", span);
        return 1;
    }
    /* Tercios de la caida, dejando fuera los extremos: arriba se ve el rio de
       la meseta y abajo la poza, y ninguno de los dos es la cortina. */
    int up0 = row_bot + (int)(span * 0.62f), up1 = row_bot + (int)(span * 0.88f);
    int lo0 = row_bot + (int)(span * 0.10f), lo1 = row_bot + (int)(span * 0.36f);

    printf("1. la malla dibuja la caida sin cortinas\n");
    draw(cam);
    long n_up = 0, n_lo = 0;
    double mean_up = band_mean(0, up0, VW - 1, up1, &n_up);
    double mean_lo = band_mean(0, lo0, VW - 1, lo1, &n_lo);
    snprintf(buf, sizeof buf, "%ld px arriba, %ld px abajo", n_up, n_lo);
    check("hay agua en el acantilado con las cortinas apagadas",
          n_up > 400 && n_lo > 400, buf);

    printf("2. la espuma crece hacia abajo\n");
    double ratio = mean_up > 0.5 ? mean_lo / mean_up : 0.0;
    snprintf(buf, sizeof buf, "brillo %.1f arriba -> %.1f abajo (x%.2f)",
             mean_up, mean_lo, ratio);
    check("el pie de la cascada es mas claro que el labio", ratio > 1.10, buf);

    printf("3. control negativo: sin caida, sin cortina\n");
    /* Una pendiente que ninguna ladera puede alcanzar: nada cuenta como caida,
       asi que el mismo encuadre tiene que perder la senal. */
    g3d_water_render_set_falls(1.0e6f, 1.5f, 1.0f, 1.0f);
    draw(cam);
    double off_up = band_mean(0, up0, VW - 1, up1, NULL);
    double off_lo = band_mean(0, lo0, VW - 1, lo1, NULL);
    double off_ratio = off_up > 0.5 ? off_lo / off_up : 0.0;
    snprintf(buf, sizeof buf, "sin caida x%.2f, con caida x%.2f", off_ratio, ratio);
    check("la senal es de la cascada y no del agua sin mas",
          off_ratio < ratio - 0.08, buf);
    g3d_water_render_set_falls(1.0f, 1.5f, 1.0f, 1.0f);

    printf("4. las cortinas viejas siguen disponibles\n");
    while (glGetError() != GL_NO_ERROR) { }
    g3d_water_falls_set_curtains(1);
    draw(cam);
    int errs = 0; GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) { printf("        GL 0x%04X\n", e); errs++; }
    double white_curtains = white_fraction();
    g3d_water_falls_set_curtains(0);
    draw(cam);
    double white_mesh = white_fraction();
    check("dibujar las cortinas no da errores de GL", errs == 0, "");
    snprintf(buf, sizeof buf, "cortinas %.1f%% blanco, malla %.1f%% blanco",
             white_curtains * 100.0, white_mesh * 100.0);
    check("una sola lamina no lava mas que el monton de quads",
          white_mesh <= white_curtains + 0.02, buf);

    printf("5. un lago entre colinas no es una cascada\n");
    g3d_water_render_shutdown();
    g3d_water_falls_shutdown();
    g3d_waterfield_shutdown();
    static float hills[S * S];
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (float)i - (S - 1) * 0.5f, z = (float)j - (S - 1) * 0.5f;
            float r = sqrtf(x * x + z * z) / ((S - 1) * 0.5f);
            /* Cuenco de paredes empinadas: el TERRENO cae mucho, el agua no. */
            hills[j * S + i] = 26.0f * r * r - 8.0f;
        }
    g3d_waterfield_init(hills, S, WS);
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);
    cam->position = vec3_make(0.0f, 14.0f, 46.0f);
    g3d_camera_look_at_impl(cam, vec3_make(0.0f, 0.0f, 0.0f), vec3_make(0, 1, 0));
    g3d_camera_update(cam);
    draw(cam);
    long lake = 0;
    band_mean(0, 0, VW - 1, VH - 1, &lake);
    double white_lake = white_fraction();
    snprintf(buf, sizeof buf, "%.1f%% de blanco sobre %ld px de agua",
             white_lake * 100.0, lake);
    check("el lago no se pinta de espuma", white_lake < 0.05, buf);
    check("y sigue viendose", lake > 20000, buf);

    g3d_water_render_shutdown();
    g3d_water_falls_shutdown();
    g3d_waterfield_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    printf(fails ? "\n%d FALLOS\n" : "\nTODO CORRECTO (%d fallos)\n", fails);
    return fails ? 1 : 0;
}
