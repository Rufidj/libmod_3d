/* Runs the real water render path against the real driver, in a hidden window.
   Catches what glslangValidator cannot: driver-specific compile/link failures,
   uniform mismatches and GL errors from the actual draw. Also renders to an
   offscreen buffer and reports how many pixels the water actually covered, so a
   silently-invisible surface fails instead of passing. */
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
#include "libmod_3d_camera.h"
#include "libmod_3d_water.h"
#include "libmod_3d_scenefile.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_water_falls.h"

#define S 129
#define WS 128.0f
#define VW 640
#define VH 480

static float terr[S * S];
static int fails = 0;

static void check(const char *what, int ok, const char *detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail && *detail ? " -- " : "", detail ? detail : "");
    if (!ok) fails++;
}

/* The driver's own explanation beats guessing from an error code. */
static void APIENTRY dbg(GLenum src, GLenum type, GLuint id, GLenum sev,
                         GLsizei len, const GLchar *msg, const void *user) {
    (void)src; (void)id; (void)len; (void)user;
    if (type == GL_DEBUG_TYPE_ERROR || sev == GL_DEBUG_SEVERITY_HIGH)
        printf("        GL DEBUG: %s\n", msg);
}

static int gl_errors(const char *stage) {
    int n = 0; GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) {
        printf("        GL error 0x%04X during %s\n", e, stage);
        n++;
    }
    return n;
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 77;   /* no display: skip, do not fail the suite */
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("water", 0, 0, VW, VH,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) { printf("no window: %s\n", SDL_GetError()); SDL_Quit(); return 77; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("no GL context: %s\n", SDL_GetError()); SDL_Quit(); return 77; }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(dbg, NULL);

    printf("1. capabilities\n");
    g3d_glcaps_print();
    const G3DGLCaps *c = g3d_glcaps();
    check("context is GL 4.x", c->major >= 4, "");
    check("tier detected as ultra", c->tier == G3D_TIER_ULTRA, g3d_tier_name(c->tier));
    check("compute reported", c->compute == 1, "");
    check("tessellation reported", c->tessellation == 1, "");

    /* A basin in the middle of a bowl-shaped island, so there is shoreline. */
    for (int j = 0; j < S; j++)
        for (int i = 0; i < S; i++) {
            float x = (float)i - (S - 1) * 0.5f, z = (float)j - (S - 1) * 0.5f;
            float r = sqrtf(x * x + z * z) / ((S - 1) * 0.5f);
            terr[j * S + i] = 14.0f * r * r - 6.0f;
        }

    printf("2. field\n");
    check("field init", g3d_waterfield_init(terr, S, WS), "");
    g3d_waterfield_set_sea_level(0.0f);
    g3d_waterfield_settle(4.0f);
    char buf[160];
    float lvl = g3d_waterfield_level_at(0.0f, 0.0f);
    snprintf(buf, sizeof(buf), "level=%.2f", lvl);
    check("water present at the centre", lvl > G3D_NO_WATER_TEST, buf);

    /* Offscreen target, so we can count what got drawn. */
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
    check("offscreen target complete",
          glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "");

    G3DCamera *cam = g3d_camera_impl_create(0 /* perspective */);
    if (!cam) { printf("no camera\n"); return 1; }
    cam->position = vec3_make(0.0f, 12.0f, 46.0f);
    cam->fov = 60.0f;
    cam->near_plane = 0.1f;
    cam->far_plane = 500.0f;
    cam->aspect_ratio = (float)VW / (float)VH;
    g3d_camera_look_at_impl(cam, vec3_make(0.0f, 0.0f, 0.0f), vec3_make(0, 1, 0));
    g3d_camera_update(cam);

    glViewport(0, 0, VW, VH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("3. tessellated path (ultra)\n");
    gl_errors("setup");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    check("no GL errors from the draw", gl_errors("tessellated draw") == 0, "");

    unsigned char *px = (unsigned char *)malloc(VW * VH * 4);
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);
    long lit = 0;
    for (int i = 0; i < VW * VH; i++)
        if (px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2]) lit++;
    double pct = 100.0 * (double)lit / (double)(VW * VH);
    snprintf(buf, sizeof(buf), "%.1f%% of the frame", pct);
    check("water actually rasterised", lit > 2000, buf);

    printf("4. flat path (mid/low fallback)\n");
    g3d_water_render_force_tessellation(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    check("no GL errors from the fallback draw", gl_errors("flat draw") == 0, "");
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);
    lit = 0;
    for (int i = 0; i < VW * VH; i++)
        if (px[i * 4] || px[i * 4 + 1] || px[i * 4 + 2]) lit++;
    pct = 100.0 * (double)lit / (double)(VW * VH);
    snprintf(buf, sizeof(buf), "%.1f%% of the frame", pct);
    check("fallback water rasterised", lit > 2000, buf);

    /* Save both for eyeballing. */
    FILE *f = fopen("/tmp/water_flat.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", VW, VH);
        for (int y = VH - 1; y >= 0; y--)
            for (int x = 0; x < VW; x++) fwrite(&px[(y * VW + x) * 4], 1, 3, f);
        fclose(f);
    }

    g3d_water_render_force_tessellation(-1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);
    f = fopen("/tmp/water_tess.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", VW, VH);
        for (int y = VH - 1; y >= 0; y--)
            for (int x = 0; x < VW; x++) fwrite(&px[(y * VW + x) * 4], 1, 3, f);
        fclose(f);
    }

    /* The path the editor and .prg scripts actually take: they never touch the
       field API, they just ask the legacy API for a sea. That MUST bring up the
       unified field, or everything silently keeps rendering the old flat plane.
       Reproduces the editor's real call order, including the case where the
       level never changes again after load (so g3d_water_create is not called a
       second time and only set_enabled keeps firing). */
    printf("5. asking for a sea through the LEGACY api starts the new water\n");
    g3d_water_render_shutdown();
    g3d_waterfield_shutdown();
    check("field is down to start with", g3d_waterfield_active() == 0, "");

    G3DMesh *tm = g3d_primitive_terrain_from_heights(S, WS, terr, 1.0f);
    check("terrain mesh built", tm != NULL, "");
    check("terrain registered as the scene heightfield",
          g3d_scene_set_terrain_collider(tm) == 1, "");

    g3d_water_create(3.0f, 4000.0f, 200);
    check("legacy g3d_water_create brought up the unified field",
          g3d_waterfield_active() == 1, "");
    snprintf(buf, sizeof(buf), "sea=%.2f", g3d_waterfield_get_sea_level());
    check("sea level reached the field",
          fabsf(g3d_waterfield_get_sea_level() - 3.0f) < 0.01f, buf);

    /* And the case that actually bit in the editor: field torn down, water still
       enabled, level unchanged -> only set_enabled runs. */
    g3d_waterfield_shutdown();
    g3d_water_set_enabled(1);
    check("set_enabled alone also restores the field",
          g3d_waterfield_active() == 1, "");

    g3d_water_set_enabled(0);
    check("disabling drains the field's sea",
          g3d_waterfield_get_sea_level() < G3D_NO_WATER_TEST, "");
    g3d_water_set_enabled(1);

    /* Moving the sea level up and down must not leave water behind. When the
       water recedes, cells that fall outside the new wet region keep whatever
       was last uploaded to them, which shows up as a wall of water standing at
       the previous level. Compare a level reached by LOWERING against the same
       level reached directly: they have to match. */
    printf("6. changing the sea level leaves no stale water\n");
    g3d_water_render_shutdown();
    g3d_waterfield_shutdown();
    g3d_waterfield_init(terr, S, WS);

    g3d_waterfield_set_sea_level(-1.0f);
    g3d_waterfield_settle(2.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    unsigned char *ref = (unsigned char *)malloc(VW * VH * 4);
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, ref);

    /* Same target level, but arrived at from a much higher sea. */
    g3d_waterfield_set_sea_level(6.0f);
    g3d_waterfield_settle(2.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    g3d_waterfield_set_sea_level(-1.0f);
    g3d_waterfield_settle(2.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g3d_water_render(cam, 0);
    glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, px);

    long differing = 0;
    for (int i = 0; i < VW * VH; i++) {
        int dr = abs((int)px[i * 4]     - (int)ref[i * 4]);
        int dg = abs((int)px[i * 4 + 1] - (int)ref[i * 4 + 1]);
        int db = abs((int)px[i * 4 + 2] - (int)ref[i * 4 + 2]);
        if (dr + dg + db > 60) differing++;
    }
    double pctDiff = 100.0 * (double)differing / (double)(VW * VH);
    snprintf(buf, sizeof(buf), "%.2f%% of pixels differ after raise+lower", pctDiff);
    check("lowered sea matches the same level reached directly", pctDiff < 1.0, buf);
    free(ref);

    /* The water reconstructs a screen UV from clip space, so the scene capture
       has to cover exactly the live viewport. Hosts that render the scene into
       their own framebuffer (the editor's scene panel) set a viewport the
       renderer was never told about; capturing the nominal render size instead
       produced hard screen-aligned rectangles across the water. */
    printf("7. the scene capture follows the actual viewport\n");
    {
        int vw = VW / 2, vh = VH / 3;
        glViewport(0, 0, vw, vh);
        unsigned int cap = g3d_renderer_capture_scene();
        check("capture returned a texture", cap != 0, "");
        GLint gw = 0, gh = 0;
        glBindTexture(GL_TEXTURE_2D, cap);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &gh);
        snprintf(buf, sizeof(buf), "captured %dx%d, viewport %dx%d", gw, gh, vw, vh);
        check("capture size matches the viewport", gw == vw && gh == vh, buf);

        unsigned int dcap = g3d_renderer_capture_depth();
        gw = gh = 0;
        glBindTexture(GL_TEXTURE_2D, dcap);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &gh);
        snprintf(buf, sizeof(buf), "captured %dx%d, viewport %dx%d", gw, gh, vw, vh);
        check("depth capture size matches the viewport", gw == vw && gh == vh, buf);
        glBindTexture(GL_TEXTURE_2D, 0);
        glViewport(0, 0, VW, VH);
    }

    /* Falls must appear where the field really drops and nowhere else. */
    printf("8. waterfalls come out of the field\n");
    {
        static float cliff[S * S];
        for (int j = 0; j < S; j++)
            for (int i = 0; i < S; i++) {
                float u = (float)i / (float)(S - 1);
                /* The plateau SLOPES toward the ledge. A flat one lets the
                   spring spread in every direction and drain off the near map
                   edge before it ever reaches the drop, so nothing falls. */
                cliff[j * S + i] = (u < 0.45f) ? (24.0f - 8.0f * u)
                                 : (u < 0.48f) ? (20.4f - 18.4f * ((u - 0.45f) / 0.03f))
                                               : 2.0f;
            }
        g3d_water_render_shutdown();
        g3d_water_falls_shutdown();
        g3d_waterfield_shutdown();

        /* flat ground, water everywhere, nothing to fall from */
        static float flat[S * S];
        for (int k = 0; k < S * S; k++) flat[k] = 0.0f;
        g3d_waterfield_init(flat, S, WS);
        g3d_waterfield_set_sea_level(3.0f);
        g3d_waterfield_settle(1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        snprintf(buf, sizeof(buf), "%d sheets", g3d_water_falls_count());
        check("flat water makes no waterfalls", g3d_water_falls_count() == 0, buf);

        /* a cliff with a spring above it */
        g3d_waterfield_shutdown();
        g3d_waterfield_init(cliff, S, WS);
        g3d_waterfield_set_evaporation(0.01f);
        g3d_waterfield_add_spring(-WS * 0.35f, 0.0f, 8.0f);
        g3d_waterfield_settle(60.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        int sheets = g3d_water_falls_count();
        snprintf(buf, sizeof(buf), "%d sheets", sheets);
        check("a river over a ledge makes a waterfall", sheets > 0, buf);
        check("no GL errors drawing the falls", gl_errors("falls draw") == 0, "");

        /* raising the threshold above the drop must retire them */
        g3d_water_falls_set_threshold(200.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        snprintf(buf, sizeof(buf), "%d sheets", g3d_water_falls_count());
        check("threshold above the drop removes them",
              g3d_water_falls_count() == 0, buf);
        g3d_water_falls_set_threshold(1.5f);
    }

    printf("9. repeated frames stay clean\n");
    int errs = 0;
    for (int i = 0; i < 30; i++) {
        g3d_waterfield_step(1.0f / 60.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g3d_water_render(cam, 0);
        errs += gl_errors("animated frame");
    }
    check("30 simulated+rendered frames, no GL errors", errs == 0, "");

    g3d_water_render_shutdown();
    g3d_waterfield_shutdown();
    check("shutdown clean", gl_errors("shutdown") == 0, "");

    free(px);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
