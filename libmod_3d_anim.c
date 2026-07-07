/*
 * libmod_3d_anim.c - Skeletal animation playback (glTF), CPU skinning
 *
 * Each frame: evaluate the animation channels to set every node's local TRS,
 * compute node world transforms, build jointMatrix[j] = nodeWorld * inverseBind,
 * then for every skinned mesh recompute vertex positions/normals on the CPU
 * (linear blend skinning) and re-upload the VBO. This keeps the GPU vertex
 * format and all shaders (lighting, shadows) unchanged.
 */

#include "libmod_3d_anim.h"
#include "libmod_3d_mesh.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int g3d_model_animation_count(G3DModel *model) {
    return model ? model->animation_count : 0;
}

float g3d_model_animation_duration(G3DModel *model, int anim) {
    if (!model || anim < 0 || anim >= model->animation_count)
        return 0.0f;
    return model->animations[anim].duration;
}

/* Sample one channel at time t into out[] (3 floats for T/S, 4 for R). */
static void sample_channel(const G3DAnimChannel *c, float t, float *out) {
    int n = c->key_count;
    int comps = (c->path == 1) ? 4 : 3;
    if (n <= 0)
        return;
    if (t <= c->times[0]) {
        memcpy(out, c->values, comps * sizeof(float));
        return;
    }
    if (t >= c->times[n - 1]) {
        memcpy(out, c->values + (n - 1) * comps, comps * sizeof(float));
        return;
    }
    int k = 0;
    while (k < n - 1 && c->times[k + 1] < t)
        k++;
    const float *a = c->values + k * comps;
    const float *b = c->values + (k + 1) * comps;
    if (c->interp == 1) { /* step */
        memcpy(out, a, comps * sizeof(float));
        return;
    }
    float span = c->times[k + 1] - c->times[k];
    float f = (span > 1e-8f) ? (t - c->times[k]) / span : 0.0f;
    if (c->path == 1) {
        /* rotation: slerp */
        Quat qa = quat_make(a[0], a[1], a[2], a[3]);
        Quat qb = quat_make(b[0], b[1], b[2], b[3]);
        Quat q = quat_normalize(quat_slerp(qa, qb, f));
        out[0] = q.x; out[1] = q.y; out[2] = q.z; out[3] = q.w;
    } else {
        for (int i = 0; i < comps; i++)
            out[i] = a[i] + (b[i] - a[i]) * f;
    }
}

/* Recursively compute a node's world transform (memoised this frame). */
static Mat4 node_world(G3DModel *m, int n, char *done) {
    if (done[n])
        return m->node_global[n];
    Mat4 local = mat4_trs(m->node_cur_t[n], m->node_cur_r[n], m->node_cur_s[n]);
    if (m->node_parent[n] >= 0)
        m->node_global[n] = mat4_multiply(node_world(m, m->node_parent[n], done), local);
    else
        m->node_global[n] = local;
    done[n] = 1;
    return m->node_global[n];
}

/* Linear blend skinning of one mesh using the current joint matrices. */
static void skin_mesh(G3DMesh *mesh, const Mat4 *JM, int joint_count,
                      const float *off) {
    for (uint32_t v = 0; v < mesh->vertex_count; v++) {
        uint16_t j[4];
        const uint16_t *jin = &mesh->vjoints[v * 4];
        for (int k = 0; k < 4; k++)
            j[k] = (jin[k] < joint_count) ? jin[k] : 0;  /* clamp bad indices */
        const float *w = &mesh->vweights[v * 4];
        float wsum = w[0] + w[1] + w[2] + w[3];

        float sm[16];
        if (wsum < 1e-6f) {
            /* unweighted vertex: keep bind pose */
            for (int i = 0; i < 16; i++) sm[i] = 0.0f;
            sm[0] = sm[5] = sm[10] = sm[15] = 1.0f;
        } else {
            for (int i = 0; i < 16; i++) {
                sm[i] = w[0] * JM[j[0]].m[i] + w[1] * JM[j[1]].m[i] +
                        w[2] * JM[j[2]].m[i] + w[3] * JM[j[3]].m[i];
            }
        }

        const float *bp = &mesh->bind_pos[v * 3];
        const float *bn = &mesh->bind_nrm[v * 3];

        /* column-major: pos.x = m0*x + m4*y + m8*z + m12 */
        float px = sm[0]*bp[0] + sm[4]*bp[1] + sm[8]*bp[2]  + sm[12];
        float py = sm[1]*bp[0] + sm[5]*bp[1] + sm[9]*bp[2]  + sm[13];
        float pz = sm[2]*bp[0] + sm[6]*bp[1] + sm[10]*bp[2] + sm[14];
        mesh->vertices[v].position[0] = px + off[0];
        mesh->vertices[v].position[1] = py + off[1];
        mesh->vertices[v].position[2] = pz + off[2];

        float nx = sm[0]*bn[0] + sm[4]*bn[1] + sm[8]*bn[2];
        float ny = sm[1]*bn[0] + sm[5]*bn[1] + sm[9]*bn[2];
        float nz = sm[2]*bn[0] + sm[6]*bn[1] + sm[10]*bn[2];
        float nl = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nl < 1e-6f) nl = 1.0f;
        mesh->vertices[v].normal[0] = nx / nl;
        mesh->vertices[v].normal[1] = ny / nl;
        mesh->vertices[v].normal[2] = nz / nl;
    }
    g3d_mesh_calculate_bounds(mesh);
    g3d_mesh_update_gpu(mesh);
}

/* Compute joint matrices from the current node TRS and skin all meshes. */
static void apply_pose(G3DModel *model) {
    char *done = (char *)calloc(model->node_count, 1);
    if (!done)
        return;
    for (int n = 0; n < model->node_count; n++)
        node_world(model, n, done);
    free(done);

    for (int j = 0; j < model->joint_count; j++)
        model->joint_matrix[j] =
            mat4_multiply(model->node_global[model->joint_node[j]], model->inverse_bind[j]);

    for (uint32_t i = 0; i < model->mesh_count; i++) {
        G3DMesh *mesh = &model->meshes[i];
        if (mesh->skinned && mesh->bind_pos && mesh->vjoints)
            skin_mesh(mesh, model->joint_matrix, model->joint_count, model->skin_offset);
    }
}

void g3d_model_set_lock_root(G3DModel *model, int enable) {
    if (model)
        model->lock_root = enable ? 1 : 0;
}

void g3d_model_rest_pose(G3DModel *model) {
    if (!model || !model->skinned)
        return;
    for (int n = 0; n < model->node_count; n++) {
        model->node_cur_t[n] = model->node_base_t[n];
        model->node_cur_r[n] = model->node_base_r[n];
        model->node_cur_s[n] = model->node_base_s[n];
    }
    apply_pose(model);
}

void g3d_model_animate(G3DModel *model, int anim, float time, int loop) {
    if (!model || !model->skinned)
        return;

    /* reset to base local TRS */
    for (int n = 0; n < model->node_count; n++) {
        model->node_cur_t[n] = model->node_base_t[n];
        model->node_cur_r[n] = model->node_base_r[n];
        model->node_cur_s[n] = model->node_base_s[n];
    }

    if (anim >= 0 && anim < model->animation_count) {
        G3DAnimation *A = &model->animations[anim];
        float t = time;
        if (A->duration > 1e-6f) {
            if (loop)
                t = fmodf(time, A->duration);
            else if (t > A->duration)
                t = A->duration;
        }
        if (t < 0.0f) t = 0.0f;

        for (int c = 0; c < A->channel_count; c++) {
            G3DAnimChannel *ch = &A->channels[c];
            if (ch->node < 0 || ch->node >= model->node_count)
                continue;
            float val[4];
            sample_channel(ch, t, val);
            if (ch->path == 0)
                model->node_cur_t[ch->node] = vec3_make(val[0], val[1], val[2]);
            else if (ch->path == 1)
                model->node_cur_r[ch->node] = quat_make(val[0], val[1], val[2], val[3]);
            else if (ch->path == 2)
                model->node_cur_s[ch->node] = vec3_make(val[0], val[1], val[2]);
        }
    }

    /* Strip root motion: keep the skeleton root translation at its base so the
       character animates in place instead of wandering off-screen. */
    if (model->lock_root && model->root_node >= 0)
        model->node_cur_t[model->root_node] = model->node_base_t[model->root_node];

    apply_pose(model);
}
