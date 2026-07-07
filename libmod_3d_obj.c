/*
 * libmod_3d_obj.c - Wavefront .obj loader (static meshes, one submesh per material).
 */
#include "libmod_3d_obj.h"
#include "libmod_3d_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- tiny growable vectors -------------------------------------------- */
typedef struct { float *d; int n, cap; } Vf;
static void vf_push(Vf *v, float x) {
    if (v->n >= v->cap) { v->cap = v->cap ? v->cap * 2 : 256; v->d = realloc(v->d, (size_t)v->cap * sizeof(float)); }
    v->d[v->n++] = x;
}
typedef struct { G3DVertex *d; int n, cap; } Vv;
static void vv_push(Vv *v, G3DVertex vert) {
    if (v->n >= v->cap) { v->cap = v->cap ? v->cap * 2 : 256; v->d = realloc(v->d, (size_t)v->cap * sizeof(G3DVertex)); }
    v->d[v->n++] = vert;
}

/* per-material submesh being accumulated */
typedef struct { char mtl[64]; Vv verts; } SubM;

#define OBJ_MAX_SUB 64

static void dir_of(const char *path, char *out, size_t n) {
    strncpy(out, path, n - 1); out[n - 1] = 0;
    char *s = strrchr(out, '/');
    if (s) s[1] = 0; else out[0] = 0;
}

/* parse the .mtl: map each material name -> map_Kd texture path (relative to dir) */
typedef struct { char name[64]; char tex[256]; } Mtl;
static int parse_mtl(const char *mtlpath, const char *dir, Mtl *out, int maxm) {
    FILE *f = fopen(mtlpath, "r");
    if (!f) return 0;
    int nm = 0; char line[512];
    while (fgets(line, sizeof(line), f)) {
        char a[256];
        if (sscanf(line, " newmtl %63s", a) == 1) {
            if (nm < maxm) { strncpy(out[nm].name, a, 63); out[nm].name[63] = 0; out[nm].tex[0] = 0; nm++; }
        } else if (nm > 0 && (sscanf(line, " map_Kd %255[^\r\n]", a) == 1)) {
            /* strip leading spaces/options; take the last token as the filename */
            char *fn = a; char *sp = strrchr(a, ' '); if (sp) fn = sp + 1;
            snprintf(out[nm - 1].tex, sizeof(out[nm - 1].tex), "%s%s", dir, fn);
        }
    }
    fclose(f);
    return nm;
}

G3DModel *g3d_obj_load(const char *filepath) {
    if (!filepath) return NULL;
    FILE *f = fopen(filepath, "r");
    if (!f) { fprintf(stderr, "G3D: obj open failed: %s\n", filepath); return NULL; }

    char dir[256]; dir_of(filepath, dir, sizeof(dir));
    Vf pos = {0}, nrm = {0}, uv = {0};
    SubM subs[OBJ_MAX_SUB]; int nsub = 0;
    int cur = -1;
    Mtl mtls[OBJ_MAX_SUB]; int nmtl = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z; sscanf(line + 2, "%f %f %f", &x, &y, &z);
            vf_push(&pos, x); vf_push(&pos, y); vf_push(&pos, z);
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z; sscanf(line + 2, "%f %f %f", &x, &y, &z);
            vf_push(&nrm, x); vf_push(&nrm, y); vf_push(&nrm, z);
        } else if (line[0] == 'v' && line[1] == 't') {
            float u = 0, v = 0; sscanf(line + 2, "%f %f", &u, &v);
            vf_push(&uv, u); vf_push(&uv, v);
        } else if (!strncmp(line, "mtllib", 6)) {
            char mf[256]; if (sscanf(line + 6, " %255s", mf) == 1) {
                char mp[512]; snprintf(mp, sizeof(mp), "%s%s", dir, mf);
                nmtl = parse_mtl(mp, dir, mtls, OBJ_MAX_SUB);
            }
        } else if (!strncmp(line, "usemtl", 6)) {
            char mn[64]; mn[0] = 0; sscanf(line + 6, " %63s", mn);
            cur = -1;
            for (int i = 0; i < nsub; i++) if (!strcmp(subs[i].mtl, mn)) { cur = i; break; }
            if (cur < 0 && nsub < OBJ_MAX_SUB) {
                cur = nsub++; memset(&subs[cur], 0, sizeof(SubM));
                strncpy(subs[cur].mtl, mn, 63);
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            if (cur < 0) { cur = nsub++; memset(&subs[cur], 0, sizeof(SubM)); subs[cur].mtl[0] = 0; }
            /* parse up to 16 face corners, each "v", "v/vt", "v//vn" or "v/vt/vn" */
            int vi[16], ti[16], ni[16], nc = 0;
            char *p = line + 2;
            while (*p && nc < 16) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == 0 || *p == '\r' || *p == '\n') break;
                int a = 0, b = 0, c = 0;
                if (sscanf(p, "%d/%d/%d", &a, &b, &c) == 3) {}
                else if (sscanf(p, "%d//%d", &a, &c) == 2) { b = 0; }
                else if (sscanf(p, "%d/%d", &a, &b) == 2) { c = 0; }
                else if (sscanf(p, "%d", &a) == 1) { b = 0; c = 0; }
                vi[nc] = a; ti[nc] = b; ni[nc] = c; nc++;
                while (*p && *p != ' ' && *p != '\t') p++;
            }
            int np = pos.n / 3, nt = uv.n / 2, nn = nrm.n / 3;
            /* triangulate as a fan */
            for (int t = 1; t + 1 < nc; t++) {
                int idx[3] = {0, t, t + 1};
                /* face normal fallback if no vn */
                float fn[3] = {0, 1, 0};
                if (ni[0] == 0) {
                    int ia = (vi[idx[0]] > 0 ? vi[idx[0]] - 1 : np + vi[idx[0]]);
                    int ib = (vi[idx[1]] > 0 ? vi[idx[1]] - 1 : np + vi[idx[1]]);
                    int ic = (vi[idx[2]] > 0 ? vi[idx[2]] - 1 : np + vi[idx[2]]);
                    if (ia >= 0 && ib >= 0 && ic >= 0 && ia < np && ib < np && ic < np) {
                        float *A = &pos.d[ia*3], *B = &pos.d[ib*3], *C = &pos.d[ic*3];
                        float e1[3] = {B[0]-A[0], B[1]-A[1], B[2]-A[2]};
                        float e2[3] = {C[0]-A[0], C[1]-A[1], C[2]-A[2]};
                        fn[0] = e1[1]*e2[2]-e1[2]*e2[1]; fn[1] = e1[2]*e2[0]-e1[0]*e2[2]; fn[2] = e1[0]*e2[1]-e1[1]*e2[0];
                        float l = sqrtf(fn[0]*fn[0]+fn[1]*fn[1]+fn[2]*fn[2]); if (l > 1e-6f) { fn[0]/=l; fn[1]/=l; fn[2]/=l; }
                    }
                }
                for (int k = 0; k < 3; k++) {
                    int c = idx[k];
                    int p_i = (vi[c] > 0 ? vi[c] - 1 : np + vi[c]);
                    int t_i = ti[c] ? (ti[c] > 0 ? ti[c] - 1 : nt + ti[c]) : -1;
                    int n_i = ni[c] ? (ni[c] > 0 ? ni[c] - 1 : nn + ni[c]) : -1;
                    G3DVertex v; memset(&v, 0, sizeof(v));
                    if (p_i >= 0 && p_i < np) { v.position[0]=pos.d[p_i*3]; v.position[1]=pos.d[p_i*3+1]; v.position[2]=pos.d[p_i*3+2]; }
                    if (n_i >= 0 && n_i < nn) { v.normal[0]=nrm.d[n_i*3]; v.normal[1]=nrm.d[n_i*3+1]; v.normal[2]=nrm.d[n_i*3+2]; }
                    else { v.normal[0]=fn[0]; v.normal[1]=fn[1]; v.normal[2]=fn[2]; }
                    if (t_i >= 0 && t_i < nt) { v.texcoord[0]=uv.d[t_i*2]; v.texcoord[1]=uv.d[t_i*2+1]; }
                    vv_push(&subs[cur].verts, v);
                }
            }
        }
    }
    fclose(f);
    free(pos.d); free(nrm.d); free(uv.d);

    /* build the model: one G3DMesh per non-empty submesh */
    int valid = 0;
    for (int i = 0; i < nsub; i++) if (subs[i].verts.n >= 3) valid++;
    if (valid == 0) { for (int i=0;i<nsub;i++) free(subs[i].verts.d); fprintf(stderr, "G3D: obj has no geometry: %s\n", filepath); return NULL; }

    G3DMesh *meshes = (G3DMesh *)calloc(valid, sizeof(G3DMesh));
    void **textures = (void **)calloc(valid, sizeof(void *));
    int out = 0;
    for (int i = 0; i < nsub; i++) {
        if (subs[i].verts.n < 3) { free(subs[i].verts.d); continue; }
        int vc = subs[i].verts.n;
        uint32_t *idx = (uint32_t *)malloc((size_t)vc * sizeof(uint32_t));
        for (int k = 0; k < vc; k++) idx[k] = (uint32_t)k;
        G3DMesh *m = g3d_mesh_create("objsub", subs[i].verts.d, (uint32_t)vc, idx, (uint32_t)vc);
        free(idx); free(subs[i].verts.d);
        if (!m) continue;
        meshes[out] = *m; free(m);   /* upload after recentering */
        /* texture from the matching material's map_Kd */
        for (int mi = 0; mi < nmtl; mi++)
            if (!strcmp(mtls[mi].name, subs[i].mtl) && mtls[mi].tex[0]) {
                G3DTexture *t = g3d_texture_load_impl(mtls[mi].tex);
                if (t) g3d_texture_upload_gpu(t);   /* GPU handle, else it renders untextured */
                textures[out] = t;
                break;
            }
        out++;
    }

    /* recenter: bottom on Y=0, centred on X/Z (so it sits on the ground when placed) */
    {
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (int s = 0; s < out; s++)
            for (uint32_t v = 0; v < meshes[s].vertex_count; v++) {
                float *p = meshes[s].vertices[v].position;
                for (int c = 0; c < 3; c++) { if (p[c] < mn[c]) mn[c] = p[c]; if (p[c] > mx[c]) mx[c] = p[c]; }
            }
        float off[3] = { -(mn[0]+mx[0])*0.5f, -mn[1], -(mn[2]+mx[2])*0.5f };
        for (int s = 0; s < out; s++) {
            for (uint32_t v = 0; v < meshes[s].vertex_count; v++) {
                meshes[s].vertices[v].position[0] += off[0];
                meshes[s].vertices[v].position[1] += off[1];
                meshes[s].vertices[v].position[2] += off[2];
            }
            g3d_mesh_calculate_bounds(&meshes[s]);
            g3d_mesh_upload_gpu(&meshes[s]);
        }
    }

    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));
    model->meshes = meshes;
    model->mesh_count = (uint32_t)out;
    model->mesh_textures = textures;
    model->albedo_texture = textures[0];
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);
    model->skinned = 0;
    g3d_model_calculate_bounds(model);
    printf("G3D: OBJ loaded: %s (%d submeshes)\n", filepath, out);
    return model;
}
