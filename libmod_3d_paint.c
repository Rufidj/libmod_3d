/*
 * libmod_3d_paint.c - Terrain texture painting implementation
 */

#include "libmod_3d_paint.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>
#include <SDL_image.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* Sample a (4-channel RGBA) texture at UV with wrapping. */
static void sample_tex(G3DTexture *t, float u, float v, unsigned char out[4]) {
    if (!t || !t->data || t->channels < 3 || t->width == 0 || t->height == 0) {
        out[0] = out[1] = out[2] = 255;
        out[3] = 255;
        return;
    }
    u = u - floorf(u);
    v = v - floorf(v);
    int x = (int)(u * (float)t->width);
    int y = (int)(v * (float)t->height);
    if (x >= (int)t->width) x = t->width - 1;
    if (y >= (int)t->height) y = t->height - 1;
    int ch = (int)t->channels;
    unsigned char *p = t->data + ((size_t)y * t->width + x) * ch;
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
    out[3] = (ch == 4) ? p[3] : 255;
}

G3DPaintCanvas *g3d_paint_create(int width, int height) {
    if (width <= 0 || height <= 0)
        return NULL;
    G3DPaintCanvas *c = (G3DPaintCanvas *)calloc(1, sizeof(G3DPaintCanvas));
    if (!c)
        return NULL;
    c->width = width;
    c->height = height;
    c->pixels = (unsigned char *)malloc((size_t)width * height * 4);
    if (!c->pixels) {
        free(c);
        return NULL;
    }
    /* opaque black */
    for (size_t i = 0; i < (size_t)width * height; i++) {
        c->pixels[i * 4 + 0] = 0;
        c->pixels[i * 4 + 1] = 0;
        c->pixels[i * 4 + 2] = 0;
        c->pixels[i * 4 + 3] = 255;
    }

#ifndef VITA
    glGenTextures(1, &c->gl_handle);
    glBindTexture(GL_TEXTURE_2D, c->gl_handle);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, c->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    c->gpu_uploaded = 1;
#endif
    return c;
}

void g3d_paint_free(G3DPaintCanvas *c) {
    if (!c)
        return;
#ifndef VITA
    if (c->gl_handle)
        glDeleteTextures(1, &c->gl_handle);
#endif
    free(c->pixels);
    free(c);
}

void g3d_paint_fill(G3DPaintCanvas *c, G3DTexture *src, float tiling) {
    if (!c || !src)
        return;
    for (int y = 0; y < c->height; y++) {
        for (int x = 0; x < c->width; x++) {
            float u = (float)x / (float)c->width;
            float v = (float)y / (float)c->height;
            unsigned char col[4];
            sample_tex(src, u * tiling, v * tiling, col);
            unsigned char *d = c->pixels + ((size_t)y * c->width + x) * 4;
            d[0] = col[0];
            d[1] = col[1];
            d[2] = col[2];
            d[3] = 255;
        }
    }
    g3d_paint_upload(c);
}

static float brush_falloff(float dist, float radius) {
    if (dist >= radius)
        return 0.0f;
    float t = dist / radius;
    float k = 1.0f - t * t;
    return k * k;
}

int g3d_terrain_paint(G3DMesh *terrain, G3DPaintCanvas *c, G3DTexture *src,
                      float src_tiling, float wx, float wz, float radius,
                      float opacity) {
    if (!terrain || terrain->terrain_side <= 1)
        return 0;
    return g3d_terrain_paint_world(c, src, src_tiling, terrain->terrain_world_size,
                                   wx, wz, radius, opacity);
}

int g3d_terrain_paint_world(G3DPaintCanvas *c, G3DTexture *src, float src_tiling,
                            float world_size, float wx, float wz, float radius,
                            float opacity) {
    if (!c || !src || world_size <= 0.0f || radius <= 0.0f)
        return 0;

    float ws = world_size;
    float half = ws * 0.5f;

    /* Brush footprint in canvas pixels */
    float cu = (wx + half) / ws;          /* 0..1 */
    float cv = (wz + half) / ws;          /* 0..1 */
    float rpx = (radius / ws) * (float)c->width;
    float rpy = (radius / ws) * (float)c->height;

    int x0 = (int)floorf(cu * c->width - rpx);
    int x1 = (int)ceilf(cu * c->width + rpx);
    int y0 = (int)floorf(cv * c->height - rpy);
    int y1 = (int)ceilf(cv * c->height + rpy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->width - 1) x1 = c->width - 1;
    if (y1 > c->height - 1) y1 = c->height - 1;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float u = (float)x / (float)c->width;
            float v = (float)y / (float)c->height;
            /* World position of this canvas pixel */
            float pwx = (u - 0.5f) * ws;
            float pwz = (v - 0.5f) * ws;
            float dx = pwx - wx;
            float dz = pwz - wz;
            float f = brush_falloff(sqrtf(dx * dx + dz * dz), radius);
            if (f <= 0.0f)
                continue;

            unsigned char col[4];
            sample_tex(src, u * src_tiling, v * src_tiling, col);

            float a = f * opacity;
            if (a > 1.0f) a = 1.0f;
            unsigned char *d = c->pixels + ((size_t)y * c->width + x) * 4;
            d[0] = (unsigned char)(d[0] + (col[0] - d[0]) * a);
            d[1] = (unsigned char)(d[1] + (col[1] - d[1]) * a);
            d[2] = (unsigned char)(d[2] + (col[2] - d[2]) * a);
            d[3] = 255;
        }
    }

    g3d_paint_upload(c);
    return 1;
}

void g3d_paint_upload(G3DPaintCanvas *c) {
    if (!c || !c->pixels)
        return;
#ifndef VITA
    glBindTexture(GL_TEXTURE_2D, c->gl_handle);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, c->width, c->height, GL_RGBA,
                    GL_UNSIGNED_BYTE, c->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

unsigned int g3d_paint_get_gl(G3DPaintCanvas *c) {
    return c ? c->gl_handle : 0;
}

void *g3d_paint_get_texture(G3DPaintCanvas *c) {
    if (!c) return NULL;
    if (!c->cached_tex) {
        G3DTexture *t = (G3DTexture *)calloc(1, sizeof(G3DTexture));
        if (!t) return NULL;
        t->width = (uint32_t)c->width;
        t->height = (uint32_t)c->height;
        t->channels = 4;
        t->gl_handle = c->gl_handle;
        t->gpu_uploaded = 1;
        c->cached_tex = t;
    }
    return c->cached_tex;
}

int g3d_paint_save(G3DPaintCanvas *c, const char *filename) {
    if (!c || !c->pixels || !filename)
        return 0;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
        c->pixels, c->width, c->height, 32, c->width * 4,
        SDL_PIXELFORMAT_RGBA32);
    if (!surf)
        return 0;
    int ok = (IMG_SavePNG(surf, filename) == 0);
    SDL_FreeSurface(surf);
    return ok;
}
