/* libmod_3d_lvl.c - formato binario de niveles (.lvl). Ver libmod_3d_lvl.h */
#include "libmod_3d_lvl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; uint8_t *d[G3D_LVL_LAYERS]; } LvlPlane;
struct G3DLvl { LvlPlane p[G3D_LVL_MAX_PLANES]; char *links; };

static void plane_clear(LvlPlane *p) {
    for (int i = 0; i < G3D_LVL_LAYERS; i++) { free(p->d[i]); p->d[i] = NULL; }
    p->w = p->h = 0;
}

G3DLvl *g3d_lvl_new(void) { return (G3DLvl *)calloc(1, sizeof(G3DLvl)); }

void g3d_lvl_free(G3DLvl *l) {
    if (!l) return;
    for (int i = 0; i < G3D_LVL_MAX_PLANES; i++) plane_clear(&l->p[i]);
    free(l->links);
    free(l);
}

int g3d_lvl_plane_exists(const G3DLvl *l, int plane) {
    return l && plane >= 0 && plane < G3D_LVL_MAX_PLANES && l->p[plane].w > 0 && l->p[plane].h > 0;
}
int g3d_lvl_plane_count(const G3DLvl *l) {
    int n = 0;
    for (int i = 0; i < G3D_LVL_MAX_PLANES; i++) if (g3d_lvl_plane_exists(l, i)) n = i + 1;
    return n;
}
int g3d_lvl_width(const G3DLvl *l, int plane) { return g3d_lvl_plane_exists(l, plane) ? l->p[plane].w : 0; }
int g3d_lvl_height(const G3DLvl *l, int plane) { return g3d_lvl_plane_exists(l, plane) ? l->p[plane].h : 0; }

int g3d_lvl_plane_alloc(G3DLvl *l, int plane, int w, int h) {
    if (!l || plane < 0 || plane >= G3D_LVL_MAX_PLANES || w < 1 || h < 1 || w > 65535 || h > 65535) return 0;
    LvlPlane *p = &l->p[plane];
    plane_clear(p);
    for (int i = 0; i < G3D_LVL_LAYERS; i++) {
        p->d[i] = (uint8_t *)calloc((size_t)w * h, 1);
        if (!p->d[i]) { plane_clear(p); return 0; }
    }
    p->w = w; p->h = h;
    return 1;
}
void g3d_lvl_plane_remove(G3DLvl *l, int plane) { if (l && plane >= 0 && plane < G3D_LVL_MAX_PLANES) plane_clear(&l->p[plane]); }

uint8_t *g3d_lvl_layer(G3DLvl *l, int plane, int layer) {
    if (!g3d_lvl_plane_exists(l, plane) || layer < 0 || layer >= G3D_LVL_LAYERS) return NULL;
    return l->p[plane].d[layer];
}
int g3d_lvl_cell(const G3DLvl *l, int plane, int layer, int x, int y) {
    if (!g3d_lvl_plane_exists(l, plane) || layer < 0 || layer >= G3D_LVL_LAYERS) return 0;
    const LvlPlane *p = &l->p[plane];
    if (x < 0 || y < 0 || x >= p->w || y >= p->h) return 0;
    return p->d[layer][(size_t)y * p->w + x];
}

const char *g3d_lvl_links(const G3DLvl *l) { return (l && l->links) ? l->links : ""; }
void g3d_lvl_set_links(G3DLvl *l, const char *text) {
    if (!l) return;
    free(l->links); l->links = NULL;
    if (text && text[0]) { size_t n = strlen(text); l->links = (char *)malloc(n + 1); if (l->links) memcpy(l->links, text, n + 1); }
}

/* ---- fichero ---- */
static void w16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void w32(FILE *f, unsigned v) { w16(f, v & 0xFFFF); w16(f, (v >> 16) & 0xFFFF); }
static int r16(FILE *f, unsigned *v) { int a = fgetc(f), b = fgetc(f); if (a < 0 || b < 0) return 0; *v = (unsigned)a | ((unsigned)b << 8); return 1; }
static int r32(FILE *f, unsigned *v) { unsigned a, b; if (!r16(f, &a) || !r16(f, &b)) return 0; *v = a | (b << 16); return 1; }

int g3d_lvl_save(const G3DLvl *l, const char *path) {
    if (!l || !path) return 0;
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);       /* se escribe aparte y se renombra: un fallo no deja el nivel a medias */
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    fwrite("BLVL", 1, 4, f); w16(f, G3D_LVL_VERSION); w16(f, 0);
    for (int i = 0; i < G3D_LVL_MAX_PLANES; i++) {
        if (!g3d_lvl_plane_exists(l, i)) continue;
        const LvlPlane *p = &l->p[i];
        size_t cells = (size_t)p->w * p->h;
        fwrite("PLAN", 1, 4, f);
        w32(f, (unsigned)(7 + G3D_LVL_LAYERS * cells));
        w16(f, (unsigned)i); w16(f, (unsigned)p->w); w16(f, (unsigned)p->h); fputc(G3D_LVL_LAYERS, f);
        for (int k = 0; k < G3D_LVL_LAYERS; k++) fwrite(p->d[k], 1, cells, f);
    }
    if (l->links && l->links[0]) { size_t n = strlen(l->links); fwrite("LNKS", 1, 4, f); w32(f, (unsigned)n); fwrite(l->links, 1, n, f); }
    int ok = (fflush(f) == 0) && !ferror(f);
    fclose(f);
    if (!ok) { remove(tmp); return 0; }
    if (rename(tmp, path) != 0) { remove(path); if (rename(tmp, path) != 0) { remove(tmp); return 0; } }
    return 1;
}

G3DLvl *g3d_lvl_load(const char *path) {
    FILE *f = path ? fopen(path, "rb") : NULL; if (!f) return NULL;
    char magic[4]; unsigned ver, rsv;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "BLVL", 4) != 0 || !r16(f, &ver) || !r16(f, &rsv) || ver < 1) { fclose(f); return NULL; }
    G3DLvl *l = g3d_lvl_new(); if (!l) { fclose(f); return NULL; }
    char tag[4]; unsigned size;
    while (fread(tag, 1, 4, f) == 4 && r32(f, &size)) {
        long next = ftell(f) + (long)size;
        if (memcmp(tag, "PLAN", 4) == 0 && size >= 7) {
            unsigned idx, w, h; int nl = -1;
            if (r16(f, &idx) && r16(f, &w) && r16(f, &h) && (nl = fgetc(f)) >= 0 && g3d_lvl_plane_alloc(l, (int)idx, (int)w, (int)h)) {
                size_t cells = (size_t)w * h;
                for (int k = 0; k < nl; k++) {
                    if (k < G3D_LVL_LAYERS) { if (fread(l->p[idx].d[k], 1, cells, f) != cells) { g3d_lvl_free(l); fclose(f); return NULL; } }
                    else fseek(f, (long)cells, SEEK_CUR);
                }
            }
        }
        if (memcmp(tag, "LNKS", 4) == 0 && size > 0 && size < 4000000) {
            char *b = (char *)malloc(size + 1);
            if (b && fread(b, 1, size, f) == size) { b[size] = 0; free(l->links); l->links = b; } else free(b);
        }
        if (fseek(f, next, SEEK_SET) != 0) break;
    }
    fclose(f);
    return l;
}
