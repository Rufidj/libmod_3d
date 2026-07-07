/*
 * libmod_3d_camera.c - Camera Management Implementation
 *
 * Handles view/projection matrix computation and frustum culling
 */

#include "libmod_3d_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
   CAMERA CREATION
   ============================================================================
 */

G3DCamera *g3d_camera_impl_create(G3DCameraType type) {
    G3DCamera *camera = (G3DCamera *)malloc(sizeof(G3DCamera));
    if (!camera)
        return NULL;

    camera->id = 1;  /* TODO: proper ID pool */
    camera->active = 0;
    camera->type = type;

    /* Default position & orientation */
    camera->position = vec3_make(0, 5, 10);
    camera->target = vec3_make(0, 0, 0);
    camera->up = vec3_make(0, 1, 0);

    camera->pitch = 0;
    camera->yaw = 0;
    camera->roll = 0;

    /* Default projection */
    if (type == G3D_CAMERA_PERSPECTIVE) {
        camera->fov = 75.0f;
        camera->aspect_ratio = 16.0f / 9.0f;
    } else {
        camera->ortho_width = 1920;
        camera->ortho_height = 1080;
    }

    camera->near_plane = 0.1f;
    camera->far_plane = 10000.0f;

    /* Initialize matrices */
    camera->view_matrix = mat4_identity();
    camera->projection_matrix = mat4_identity();
    camera->view_projection = mat4_identity();

    camera->view_dirty = 1;
    camera->proj_dirty = 1;
    camera->frustum_valid = 0;

    return camera;
}

/* ============================================================================
   CAMERA POSITIONING
   ============================================================================
 */

void g3d_camera_set_position_impl(G3DCamera *camera, Vec3 position) {
    if (!camera)
        return;

    camera->position = position;
    camera->view_dirty = 1;
}

void g3d_camera_look_at_impl(G3DCamera *camera, Vec3 target, Vec3 up) {
    if (!camera)
        return;

    camera->target = target;
    camera->up = up;
    camera->view_dirty = 1;
}

void g3d_camera_set_rotation(G3DCamera *camera, float pitch, float yaw,
                             float roll) {
    if (!camera)
        return;

    camera->pitch = pitch;
    camera->yaw = yaw;
    camera->roll = roll;
    camera->view_dirty = 1;
}

void g3d_camera_rotate(G3DCamera *camera, float delta_pitch, float delta_yaw) {
    if (!camera)
        return;

    camera->pitch += delta_pitch;
    camera->yaw += delta_yaw;

    /* Clamp pitch to avoid gimbal lock */
    if (camera->pitch > G3D_PI / 2)
        camera->pitch = G3D_PI / 2;
    if (camera->pitch < -G3D_PI / 2)
        camera->pitch = -G3D_PI / 2;

    camera->view_dirty = 1;
}

/* ============================================================================
   CAMERA MOVEMENT
   ============================================================================
 */

void g3d_camera_move_forward(G3DCamera *camera, float distance) {
    if (!camera)
        return;

    Vec3 forward = g3d_camera_get_forward(camera);
    camera->position = vec3_add(camera->position, vec3_scale(forward, distance));
    camera->view_dirty = 1;
}

void g3d_camera_strafe(G3DCamera *camera, float distance) {
    if (!camera)
        return;

    Vec3 right = g3d_camera_get_right(camera);
    camera->position = vec3_add(camera->position, vec3_scale(right, distance));
    camera->view_dirty = 1;
}

void g3d_camera_move_vertical(G3DCamera *camera, float distance) {
    if (!camera)
        return;

    camera->position.y += distance;
    camera->view_dirty = 1;
}

/* ============================================================================
   PROJECTION SETTINGS
   ============================================================================
 */

void g3d_camera_set_perspective(G3DCamera *camera, float fov, float aspect,
                                float near, float far) {
    if (!camera)
        return;

    camera->type = G3D_CAMERA_PERSPECTIVE;
    camera->fov = fov;
    camera->aspect_ratio = aspect;
    camera->near_plane = near;
    camera->far_plane = far;
    camera->proj_dirty = 1;
}

void g3d_camera_set_orthographic(G3DCamera *camera, float width, float height,
                                 float near, float far) {
    if (!camera)
        return;

    camera->type = G3D_CAMERA_ORTHOGRAPHIC;
    camera->ortho_width = width;
    camera->ortho_height = height;
    camera->near_plane = near;
    camera->far_plane = far;
    camera->proj_dirty = 1;
}

/* ============================================================================
   MATRIX COMPUTATION
   ============================================================================
 */

void g3d_camera_update(G3DCamera *camera) {
    if (!camera)
        return;

    /* Update view matrix */
    if (camera->view_dirty) {
        camera->view_matrix =
            mat4_look_at(camera->position, camera->target, camera->up);
        camera->view_dirty = 0;
        camera->frustum_valid = 0;
    }

    /* Update projection matrix */
    if (camera->proj_dirty) {
        if (camera->type == G3D_CAMERA_PERSPECTIVE) {
            camera->projection_matrix = mat4_perspective(
                camera->fov, camera->aspect_ratio, camera->near_plane,
                camera->far_plane);
        } else {
            float half_width = camera->ortho_width / 2.0f;
            float half_height = camera->ortho_height / 2.0f;
            camera->projection_matrix =
                mat4_ortho(-half_width, half_width, -half_height, half_height,
                           camera->near_plane, camera->far_plane);
        }
        camera->proj_dirty = 0;
        camera->frustum_valid = 0;
    }

    /* Compute view-projection */
    camera->view_projection =
        mat4_multiply(camera->projection_matrix, camera->view_matrix);
}

Mat4 g3d_camera_get_view(G3DCamera *camera) {
    if (!camera)
        return mat4_identity();

    return camera->view_matrix;
}

Mat4 g3d_camera_get_projection(G3DCamera *camera) {
    if (!camera)
        return mat4_identity();

    return camera->projection_matrix;
}

Mat4 g3d_camera_get_view_projection(G3DCamera *camera) {
    if (!camera)
        return mat4_identity();

    return camera->view_projection;
}

/* ============================================================================
   DIRECTION VECTORS
   ============================================================================
 */

Vec3 g3d_camera_get_forward(G3DCamera *camera) {
    if (!camera)
        return vec3_forward();

    /* Forward = target - position, normalized */
    Vec3 forward = vec3_normalize(vec3_sub(camera->target, camera->position));
    return forward;
}

Vec3 g3d_camera_get_right(G3DCamera *camera) {
    if (!camera)
        return vec3_right();

    /* Right = forward x up */
    Vec3 forward = g3d_camera_get_forward(camera);
    Vec3 right = vec3_normalize(vec3_cross(forward, camera->up));
    return right;
}

Vec3 g3d_camera_get_up(G3DCamera *camera) {
    if (!camera)
        return vec3_up();

    return camera->up;
}

/* ============================================================================
   FRUSTUM CULLING
   ============================================================================
 */

void g3d_camera_update_frustum(G3DCamera *camera) {
    if (!camera || camera->frustum_valid)
        return;

    /* Extract frustum planes from view-projection matrix
     * Planes are: left, right, top, bottom, near, far
     * Format: ax + by + cz + d = 0 (normalized)
     */

    Mat4 vp = camera->view_projection;
    float *m = vp.m;

    /* Left plane */
    camera->frustum_planes[0][0] = m[3] + m[0];
    camera->frustum_planes[0][1] = m[7] + m[4];
    camera->frustum_planes[0][2] = m[11] + m[8];
    camera->frustum_planes[0][3] = m[15] + m[12];

    /* Right plane */
    camera->frustum_planes[1][0] = m[3] - m[0];
    camera->frustum_planes[1][1] = m[7] - m[4];
    camera->frustum_planes[1][2] = m[11] - m[8];
    camera->frustum_planes[1][3] = m[15] - m[12];

    /* Top plane */
    camera->frustum_planes[2][0] = m[3] - m[1];
    camera->frustum_planes[2][1] = m[7] - m[5];
    camera->frustum_planes[2][2] = m[11] - m[9];
    camera->frustum_planes[2][3] = m[15] - m[13];

    /* Bottom plane */
    camera->frustum_planes[3][0] = m[3] + m[1];
    camera->frustum_planes[3][1] = m[7] + m[5];
    camera->frustum_planes[3][2] = m[11] + m[9];
    camera->frustum_planes[3][3] = m[15] + m[13];

    /* Near plane */
    camera->frustum_planes[4][0] = m[3] + m[2];
    camera->frustum_planes[4][1] = m[7] + m[6];
    camera->frustum_planes[4][2] = m[11] + m[10];
    camera->frustum_planes[4][3] = m[15] + m[14];

    /* Far plane */
    camera->frustum_planes[5][0] = m[3] - m[2];
    camera->frustum_planes[5][1] = m[7] - m[6];
    camera->frustum_planes[5][2] = m[11] - m[10];
    camera->frustum_planes[5][3] = m[15] - m[14];

    /* Normalize planes */
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(camera->frustum_planes[i][0] *
                              camera->frustum_planes[i][0] +
                          camera->frustum_planes[i][1] *
                              camera->frustum_planes[i][1] +
                          camera->frustum_planes[i][2] *
                              camera->frustum_planes[i][2]);

        if (len > 1e-6f) {
            camera->frustum_planes[i][0] /= len;
            camera->frustum_planes[i][1] /= len;
            camera->frustum_planes[i][2] /= len;
            camera->frustum_planes[i][3] /= len;
        }
    }

    camera->frustum_valid = 1;
}

int g3d_camera_frustum_contains_point(G3DCamera *camera, Vec3 point) {
    if (!camera)
        return 1;

    g3d_camera_update_frustum(camera);

    for (int i = 0; i < 6; i++) {
        float dist = camera->frustum_planes[i][0] * point.x +
                     camera->frustum_planes[i][1] * point.y +
                     camera->frustum_planes[i][2] * point.z +
                     camera->frustum_planes[i][3];

        if (dist < 0)
            return 0;  /* Outside */
    }

    return 1;  /* Inside */
}

int g3d_camera_frustum_contains_sphere(G3DCamera *camera, Vec3 center,
                                       float radius) {
    if (!camera)
        return 1;

    g3d_camera_update_frustum(camera);

    for (int i = 0; i < 6; i++) {
        float dist = camera->frustum_planes[i][0] * center.x +
                     camera->frustum_planes[i][1] * center.y +
                     camera->frustum_planes[i][2] * center.z +
                     camera->frustum_planes[i][3];

        if (dist < -radius)
            return 0;  /* Completely outside */
    }

    return 1;  /* Inside or intersecting */
}

int g3d_camera_frustum_contains_aabb(G3DCamera *camera, Vec3 aabb_min,
                                     Vec3 aabb_max) {
    if (!camera)
        return 1;

    g3d_camera_update_frustum(camera);

    for (int i = 0; i < 6; i++) {
        float plane_x = camera->frustum_planes[i][0];
        float plane_y = camera->frustum_planes[i][1];
        float plane_z = camera->frustum_planes[i][2];
        float plane_d = camera->frustum_planes[i][3];

        /* Find closest point on AABB to plane */
        float px = (plane_x > 0) ? aabb_max.x : aabb_min.x;
        float py = (plane_y > 0) ? aabb_max.y : aabb_min.y;
        float pz = (plane_z > 0) ? aabb_max.z : aabb_min.z;

        float dist = plane_x * px + plane_y * py + plane_z * pz + plane_d;

        if (dist < 0)
            return 0;  /* AABB completely outside */
    }

    return 1;  /* AABB inside or intersecting */
}

/* ============================================================================
   CLEANUP
   ============================================================================
 */

void g3d_camera_free(G3DCamera *camera) {
    if (!camera)
        return;

    free(camera);
}
