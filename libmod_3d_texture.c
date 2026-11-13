/*
 * libmod_3d_texture.c - Texture Loading and Management Implementation
 *
 * Loads textures via SDL2_image and manages GPU texture objects
 */

#include "libmod_3d_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "SDL2/SDL_image.h"
#else
#include "libmod_ray_vita_gl.h"
#endif

/* ============================================================================
   TEXTURE LOADING
   ============================================================================
 */

#ifndef VITA
/* Convert an SDL surface to a G3DTexture (RGBA), freeing the surface. Shared by
   the file loader and the in-memory loader (embedded FBX textures). */
static G3DTexture *surface_to_g3d(SDL_Surface *surface, const char *name) {
    if (!surface) return NULL;

    /* Una PNG con paleta (8 bits, "indexada") decodifica con BytesPerPixel=1,
       igual que una en escala de grises -- pero ese byte no es un nivel de
       gris, es el INDICE de un color en la paleta. Tratado como gris (lo que
       hace el resto de esta funcion para channels==1), una paleta con pocos
       colores (indices bajos, 0..40 por ejemplo) sale casi negra sea cual sea
       el color real que apunte cada indice. Se convierte a RGBA primero, que
       ya resuelve la paleta a colores de verdad, y el resto de la funcion
       trabaja igual que con cualquier PNG/JPG sin paleta. */
    if (surface->format->palette) {
        SDL_Surface *conv = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surface);
        if (!conv) return NULL;
        surface = conv;
    }

    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    if (!texture) {
        SDL_FreeSurface(surface);
        return NULL;
    }

    memset(texture, 0, sizeof(G3DTexture));

    texture->id = 1;  /* TODO: Use proper texture ID pool */
    strncpy(texture->name, name ? name : "tex", 63);
    texture->name[63] = '\0';
    strncpy(texture->filepath, name ? name : "tex", 255);
    texture->filepath[255] = '\0';

    texture->width = surface->w;
    texture->height = surface->h;
    texture->channels = surface->format->BytesPerPixel;

    /* Copy pixel data (convert to RGBA if needed). Walk rows using surface->pitch,
       not width*BytesPerPixel -- SDL pads each row's pitch up to a 4-byte
       boundary (e.g. a 413px-wide RGB24 row is 1239 bytes but the pitch is
       1240), so a linear walk of surface->pixels drifts by that padding on
       every row and smears the channels together over the image. This only
       bites odd-sized 3-byte-per-pixel (JPEG) surfaces since 4-byte-per-pixel
       ones are always already aligned. */
    uint32_t pixel_count = texture->width * texture->height;
    int pitch = surface->pitch;

    if (texture->channels == 3) {
        /* RGB -> RGBA conversion */
        texture->channels = 4;
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);

        uint8_t *dst = texture->data;

        for (uint32_t y = 0; y < texture->height; y++) {
            uint8_t *src = (uint8_t *)surface->pixels + (size_t)y * pitch;
            for (uint32_t x = 0; x < texture->width; x++) {
                dst[0] = src[0];  /* R */
                dst[1] = src[1];  /* G */
                dst[2] = src[2];  /* B */
                dst[3] = 255;     /* A */

                src += 3;
                dst += 4;
            }
        }
    } else if (texture->channels == 4) {
        /* RGBA copy, row by row (pitch may still exceed width*4 in principle) */
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);

        for (uint32_t y = 0; y < texture->height; y++) {
            memcpy(texture->data + (size_t)y * texture->width * 4,
                   (uint8_t *)surface->pixels + (size_t)y * pitch,
                   (size_t)texture->width * 4);
        }
    } else if (texture->channels == 1) {
        /* Grayscale -> RGBA */
        texture->channels = 4;
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);

        uint8_t *dst = texture->data;

        for (uint32_t y = 0; y < texture->height; y++) {
            uint8_t *src = (uint8_t *)surface->pixels + (size_t)y * pitch;
            for (uint32_t x = 0; x < texture->width; x++) {
                dst[0] = dst[1] = dst[2] = src[0];  /* Gray → RGB */
                dst[3] = 255;                        /* A */

                src += 1;
                dst += 4;
            }
        }
    } else {
        fprintf(stderr, "G3D: Unsupported texture format (channels=%d)\n",
                texture->channels);
        free(texture->data);
        free(texture);
        SDL_FreeSurface(surface);
        return NULL;
    }

    SDL_FreeSurface(surface);

    printf("G3D: Texture loaded: %dx%d, RGBA\n", texture->width,
           texture->height);

    return texture;
}
#endif

G3DTexture *g3d_texture_load_impl(const char *filepath) {
    if (!filepath) {
        fprintf(stderr, "G3D: No filepath provided for texture\n");
        return NULL;
    }
    printf("G3D: Loading texture: %s\n", filepath);
#ifndef VITA
    SDL_Surface *surface = IMG_Load(filepath);
    if (!surface) {
        fprintf(stderr, "G3D: Failed to load texture: %s\n", IMG_GetError());
        return NULL;
    }
    return surface_to_g3d(surface, filepath);
#else
    /* VITA: Stub */
    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    memset(texture, 0, sizeof(G3DTexture));
    strncpy(texture->filepath, filepath, 255);
    return texture;
#endif
}

/* Load a texture from an in-memory encoded image (PNG/JPG/etc. bytes), e.g. a
   texture embedded inside an FBX. NULL on failure. */
G3DTexture *g3d_texture_load_mem(const char *name, const void *data, unsigned long size) {
#ifndef VITA
    if (!data || size == 0) return NULL;
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (!rw) return NULL;
    SDL_Surface *surface = IMG_Load_RW(rw, 1);   /* 1 = free the RWops */
    if (!surface) {
        fprintf(stderr, "G3D: embedded texture decode failed: %s\n", IMG_GetError());
        return NULL;
    }
    printf("G3D: embedded texture '%s' %dx%d\n", name ? name : "?", surface->w, surface->h);
    return surface_to_g3d(surface, name);
#else
    (void)name; (void)data; (void)size; return NULL;
#endif
}

G3DTexture *g3d_texture_create_from_data(const char *name, uint32_t width,
                                          uint32_t height, uint32_t channels,
                                          uint8_t *data) {
    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    if (!texture)
        return NULL;

    memset(texture, 0, sizeof(G3DTexture));

    strncpy(texture->name, name, 63);
    texture->name[63] = '\0';

    texture->width = width;
    texture->height = height;
    texture->channels = channels;
    texture->data_size = width * height * channels;

    if (data) {
        texture->data = (uint8_t *)malloc(texture->data_size);
        if (!texture->data) {
            free(texture);
            return NULL;
        }
        memcpy(texture->data, data, texture->data_size);
    }

    return texture;
}

/* ============================================================================
   GPU UPLOAD
   ============================================================================
 */

int g3d_texture_upload_gpu(G3DTexture *texture) {
    if (!texture || !texture->data)
        return 0;

    if (texture->gpu_uploaded)
        return 1;  /* Already uploaded */

#ifndef VITA
    glGenTextures(1, &texture->gl_handle);
    glBindTexture(GL_TEXTURE_2D, texture->gl_handle);

    /* Texture parameters: trilinear with mipmaps to kill minification aliasing
       (the "static/noise" look when a detailed texture is tiled into the
       distance). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* Upload to GPU */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->data);
    glGenerateMipmap(GL_TEXTURE_2D);

    texture->gpu_uploaded = 1;

    printf("G3D: Texture uploaded to GPU (handle=%u)\n", texture->gl_handle);

    return 1;
#else
    texture->gpu_uploaded = 1;
    return 1;
#endif
}

/* ============================================================================
   DEFERRED UPLOAD (glTF): the CPU work of a mip chain (box filter) runs on
   worker threads; only the GL calls stay on the thread that owns the context.
   The driver's glGenerateMipmap is single-threaded and slow on PS5, so this
   path replaces it for batches of textures.
   ============================================================================
 */

#ifndef VITA
#define G3D_MIP_MAX_LEVELS 16
#define G3D_MIP_THREADS 4

typedef struct {
    G3DTexture *tex;
    uint32_t nlev;
    uint32_t lw[G3D_MIP_MAX_LEVELS], lh[G3D_MIP_MAX_LEVELS];
    uint8_t *lev[G3D_MIP_MAX_LEVELS];   /* lev[0] is tex->data (not owned) */
} PendingTex;

typedef struct { int start, step; } MipWorkArg;

static PendingTex *g_pending = NULL;
static int g_npending = 0, g_pending_cap = 0;

/* Box-filter the full chain into owned buffers. Pure CPU, thread-safe. */
static void build_mip_chain(PendingTex *p) {
    G3DTexture *t = p->tex;
    uint32_t w = t->width, h = t->height;
    const uint8_t *src = t->data;

    p->nlev = 1;
    p->lw[0] = w; p->lh[0] = h; p->lev[0] = t->data;

    while ((w > 1 || h > 1) && p->nlev < G3D_MIP_MAX_LEVELS) {
        uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        uint8_t *dst = (uint8_t *)malloc((size_t)nw * nh * 4);
        if (!dst) break;
        for (uint32_t y = 0; y < nh; y++) {
            uint32_t sy0 = y * 2, sy1 = sy0 + 1 < h ? sy0 + 1 : h - 1;
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t sx0 = x * 2, sx1 = sx0 + 1 < w ? sx0 + 1 : w - 1;
                const uint8_t *a = src + ((size_t)sy0 * w + sx0) * 4;
                const uint8_t *b = src + ((size_t)sy0 * w + sx1) * 4;
                const uint8_t *c = src + ((size_t)sy1 * w + sx0) * 4;
                const uint8_t *d = src + ((size_t)sy1 * w + sx1) * 4;
                uint8_t *o = dst + ((size_t)y * nw + x) * 4;
                for (int k = 0; k < 4; k++)
                    o[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) / 4);
            }
        }
        p->lev[p->nlev] = dst;
        p->lw[p->nlev] = nw; p->lh[p->nlev] = nh;
        p->nlev++;
        src = dst; w = nw; h = nh;
    }
}

static int mip_worker(void *arg) {
    MipWorkArg *a = (MipWorkArg *)arg;
    for (int i = a->start; i < g_npending; i += a->step)
        build_mip_chain(&g_pending[i]);
    return 0;
}

/* GL side of one texture: all levels from the prepared chain. Main thread only. */
static void upload_prepared(PendingTex *p) {
    G3DTexture *t = p->tex;
    glGenTextures(1, &t->gl_handle);
    glBindTexture(GL_TEXTURE_2D, t->gl_handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    p->nlev > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (uint32_t l = 0; l < p->nlev; l++)
        glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)p->lw[l], (GLsizei)p->lh[l],
                     0, GL_RGBA, GL_UNSIGNED_BYTE, p->lev[l]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)p->nlev - 1);
    t->gpu_uploaded = 1;
    for (uint32_t l = 1; l < p->nlev; l++) free(p->lev[l]);
}
#endif

/* Queue a texture for the next g3d_texture_flush_pending(). Until then it has
   CPU data only (gl_handle == 0), so it must not be bound before the flush. */
void g3d_texture_defer_upload(G3DTexture *tex) {
    if (!tex || !tex->data || tex->gpu_uploaded) return;
#ifndef VITA
    if (g_npending == g_pending_cap) {
        int cap = g_pending_cap ? g_pending_cap * 2 : 256;
        PendingTex *n = (PendingTex *)realloc(g_pending, (size_t)cap * sizeof *n);
        if (!n) { g3d_texture_upload_gpu(tex); return; }
        g_pending = n; g_pending_cap = cap;
    }
    memset(&g_pending[g_npending], 0, sizeof g_pending[0]);
    g_pending[g_npending++].tex = tex;
#else
    g3d_texture_upload_gpu(tex);
#endif
}

/* Build every queued mip chain on worker threads, then upload on this thread. */
void g3d_texture_flush_pending(void) {
#ifndef VITA
    if (g_npending == 0) return;
    int nthreads = g_npending < G3D_MIP_THREADS ? g_npending : G3D_MIP_THREADS;
    SDL_Thread *th[G3D_MIP_THREADS] = {0};
    MipWorkArg arg[G3D_MIP_THREADS];
    for (int k = 0; k < nthreads; k++) {
        arg[k].start = k; arg[k].step = nthreads;
        th[k] = SDL_CreateThread(mip_worker, "g3d_mip", &arg[k]);
        if (!th[k]) mip_worker(&arg[k]);   /* no thread: do it here */
    }
    for (int k = 0; k < nthreads; k++)
        if (th[k]) SDL_WaitThread(th[k], NULL);
    for (int i = 0; i < g_npending; i++)
        upload_prepared(&g_pending[i]);
#endif
    g_npending = 0;
}

void g3d_texture_bind(G3DTexture *texture, int texture_unit) {
    if (!texture || !texture->gpu_uploaded)
        return;

#ifndef VITA
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture->gl_handle);
#endif
}

void g3d_texture_free(G3DTexture *texture) {
    if (!texture)
        return;

#ifndef VITA
    if (texture->gl_handle) {
        glDeleteTextures(1, &texture->gl_handle);
    }
#endif

    if (texture->data) {
        free(texture->data);
    }

    free(texture);
}

/* ============================================================================
   DEFAULT TEXTURES
   ============================================================================
 */

G3DTexture *g3d_texture_create_default_white(void) {
    /* Create 4x4 white texture */
    uint32_t size = 4;
    uint32_t pixel_count = size * size;
    uint8_t *data = (uint8_t *)malloc(pixel_count * 4);

    for (uint32_t i = 0; i < pixel_count; i++) {
        data[i * 4 + 0] = 255;  /* R */
        data[i * 4 + 1] = 255;  /* G */
        data[i * 4 + 2] = 255;  /* B */
        data[i * 4 + 3] = 255;  /* A */
    }

    G3DTexture *texture = g3d_texture_create_from_data("default_white", size,
                                                        size, 4, data);
    free(data);

    if (texture) {
        g3d_texture_upload_gpu(texture);
    }

    return texture;
}

G3DTexture *g3d_texture_create_default_normal(void) {
    /* Create 4x4 normal map (blue = up) */
    uint32_t size = 4;
    uint32_t pixel_count = size * size;
    uint8_t *data = (uint8_t *)malloc(pixel_count * 4);

    for (uint32_t i = 0; i < pixel_count; i++) {
        data[i * 4 + 0] = 128;  /* R (normal X) */
        data[i * 4 + 1] = 128;  /* G (normal Y) */
        data[i * 4 + 2] = 255;  /* B (normal Z, pointing up) */
        data[i * 4 + 3] = 255;  /* A */
    }

    G3DTexture *texture = g3d_texture_create_from_data("default_normal", size,
                                                        size, 4, data);
    free(data);

    if (texture) {
        g3d_texture_upload_gpu(texture);
    }

    return texture;
}
