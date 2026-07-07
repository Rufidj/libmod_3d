/*
 * libmod_3d_camera.h - Camera Management
 *
 * Manages camera matrices (view/projection) for rendering
 * Supports multiple camera types: FPS, third-person, orthographic
 */

#ifndef __LIBMOD_3D_CAMERA_H
#define __LIBMOD_3D_CAMERA_H

#include "libmod_3d_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   CAMERA TYPES
   ============================================================================
 */

typedef enum {
    G3D_CAMERA_PERSPECTIVE,     /* Standard 3D camera (FOV-based) */
    G3D_CAMERA_ORTHOGRAPHIC,    /* 2D/UI camera */
} G3DCameraType;

/* ============================================================================
   CAMERA STRUCTURE
   ============================================================================
 */

typedef struct {
    int id;
    int active;

    /* Position & Orientation */
    Vec3 position;              /* Camera position in world space */
    Vec3 target;                /* Look-at point */
    Vec3 up;                    /* Up vector */

    /* Euler angles (alternative to target) */
    float pitch;                /* Rotation around X (up/down) */
    float yaw;                  /* Rotation around Y (left/right) */
    float roll;                 /* Rotation around Z (tilt) */

    /* Projection settings */
    G3DCameraType type;
    float fov;                  /* Vertical FOV in degrees (perspective only) */
    float near_plane;
    float far_plane;
    float aspect_ratio;

    /* Orthographic specific */
    float ortho_width;
    float ortho_height;

    /* Computed matrices */
    Mat4 view_matrix;
    Mat4 projection_matrix;
    Mat4 view_projection;       /* Combined for MVP */

    /* Frustum (for culling) */
    float frustum_planes[6][4]; /* 6 planes: left, right, top, bottom, near, far */
    int frustum_valid;

    /* Dirty flags */
    int view_dirty;
    int proj_dirty;
} G3DCamera;

/* ============================================================================
   CAMERA API
   ============================================================================
 */

/* Create camera */
G3DCamera *g3d_camera_impl_create(G3DCameraType type);

/* Set position */
void g3d_camera_set_position_impl(G3DCamera *camera, Vec3 position);

/* Look at target */
void g3d_camera_look_at_impl(G3DCamera *camera, Vec3 target, Vec3 up);

/* Set rotation (Euler angles) */
void g3d_camera_set_rotation(G3DCamera *camera, float pitch, float yaw,
                             float roll);

/* Update rotation via mouse/input */
void g3d_camera_rotate(G3DCamera *camera, float delta_pitch, float delta_yaw);

/* Move camera forward/back (along look direction) */
void g3d_camera_move_forward(G3DCamera *camera, float distance);

/* Move camera left/right (strafe) */
void g3d_camera_strafe(G3DCamera *camera, float distance);

/* Move camera up/down */
void g3d_camera_move_vertical(G3DCamera *camera, float distance);

/* Set perspective projection */
void g3d_camera_set_perspective(G3DCamera *camera, float fov, float aspect,
                                float near, float far);

/* Set orthographic projection */
void g3d_camera_set_orthographic(G3DCamera *camera, float width, float height,
                                 float near, float far);

/* Update matrices (call after modifying position/rotation) */
void g3d_camera_update(G3DCamera *camera);

/* Get view matrix */
Mat4 g3d_camera_get_view(G3DCamera *camera);

/* Get projection matrix */
Mat4 g3d_camera_get_projection(G3DCamera *camera);

/* Get view-projection (MVP for geometry) */
Mat4 g3d_camera_get_view_projection(G3DCamera *camera);

/* Get forward/right/up vectors */
Vec3 g3d_camera_get_forward(G3DCamera *camera);
Vec3 g3d_camera_get_right(G3DCamera *camera);
Vec3 g3d_camera_get_up(G3DCamera *camera);

/* Update frustum for culling */
void g3d_camera_update_frustum(G3DCamera *camera);

/* Check if point is in frustum */
int g3d_camera_frustum_contains_point(G3DCamera *camera, Vec3 point);

/* Check if sphere is in frustum */
int g3d_camera_frustum_contains_sphere(G3DCamera *camera, Vec3 center,
                                       float radius);

/* Check if AABB is in frustum */
int g3d_camera_frustum_contains_aabb(G3DCamera *camera, Vec3 aabb_min,
                                     Vec3 aabb_max);

/* Free camera */
void g3d_camera_free(G3DCamera *camera);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_CAMERA_H */
