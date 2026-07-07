/*
 * libmod_3d_math.h - Vector and Matrix Math for libmod_3d
 *
 * Provides foundation math operations for 3D graphics
 * - Vector operations (Vec2, Vec3, Vec4)
 * - Quaternion operations (Quat)
 * - Matrix operations (Mat4, 4x4 column-major)
 * - Transformation utilities
 *
 * No external dependencies (uses standard C math)
 * Designed for inline performance where possible
 */

#ifndef __LIBMOD_3D_MATH_H
#define __LIBMOD_3D_MATH_H

#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   CONSTANTS
   ============================================================================
 */

#define G3D_PI 3.14159265358979323846f
#define G3D_DEG_TO_RAD (G3D_PI / 180.0f)
#define G3D_RAD_TO_DEG (180.0f / G3D_PI)
#define G3D_EPSILON 1e-6f

/* ============================================================================
   VEC2 - 2D Vector
   ============================================================================
 */

typedef struct {
    float x, y;
} Vec2;

static inline Vec2 vec2_make(float x, float y) {
    return (Vec2){x, y};
}

static inline Vec2 vec2_add(Vec2 a, Vec2 b) {
    return (Vec2){a.x + b.x, a.y + b.y};
}

static inline Vec2 vec2_sub(Vec2 a, Vec2 b) {
    return (Vec2){a.x - b.x, a.y - b.y};
}

static inline Vec2 vec2_scale(Vec2 v, float s) {
    return (Vec2){v.x * s, v.y * s};
}

static inline float vec2_dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static inline float vec2_length(Vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline Vec2 vec2_normalize(Vec2 v) {
    float len = vec2_length(v);
    if (len < G3D_EPSILON)
        return (Vec2){0, 0};
    return (Vec2){v.x / len, v.y / len};
}

/* ============================================================================
   VEC3 - 3D Vector (Most Used)
   ============================================================================
 */

typedef struct {
    float x, y, z;
} Vec3;

static inline Vec3 vec3_make(float x, float y, float z) {
    return (Vec3){x, y, z};
}

static inline Vec3 vec3_zero(void) {
    return (Vec3){0, 0, 0};
}

static inline Vec3 vec3_one(void) {
    return (Vec3){1, 1, 1};
}

static inline Vec3 vec3_up(void) {
    return (Vec3){0, 1, 0};
}

static inline Vec3 vec3_right(void) {
    return (Vec3){1, 0, 0};
}

static inline Vec3 vec3_forward(void) {
    return (Vec3){0, 0, -1};
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
    return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline Vec3 vec3_scale(Vec3 v, float s) {
    return (Vec3){v.x * s, v.y * s, v.z * s};
}

static inline Vec3 vec3_mul(Vec3 a, Vec3 b) {
    return (Vec3){a.x * b.x, a.y * b.y, a.z * b.z};
}

static inline float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                  a.x * b.y - a.y * b.x};
}

static inline float vec3_length(Vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float vec3_length_sq(Vec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len < G3D_EPSILON)
        return (Vec3){0, 0, 0};
    return (Vec3){v.x / len, v.y / len, v.z / len};
}

static inline float vec3_distance(Vec3 a, Vec3 b) {
    return vec3_length(vec3_sub(a, b));
}

static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t) {
    return vec3_add(vec3_scale(a, 1.0f - t), vec3_scale(b, t));
}

static inline Vec3 vec3_negate(Vec3 v) {
    return (Vec3){-v.x, -v.y, -v.z};
}

/* ============================================================================
   VEC4 - 4D Vector (Homogeneous coordinates)
   ============================================================================
 */

typedef struct {
    float x, y, z, w;
} Vec4;

static inline Vec4 vec4_make(float x, float y, float z, float w) {
    return (Vec4){x, y, z, w};
}

static inline Vec4 vec4_from_vec3(Vec3 v, float w) {
    return (Vec4){v.x, v.y, v.z, w};
}

static inline Vec3 vec4_to_vec3(Vec4 v) {
    return (Vec3){v.x, v.y, v.z};
}

static inline Vec4 vec4_add(Vec4 a, Vec4 b) {
    return (Vec4){a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

static inline Vec4 vec4_scale(Vec4 v, float s) {
    return (Vec4){v.x * s, v.y * s, v.z * s, v.w * s};
}

static inline float vec4_dot(Vec4 a, Vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/* ============================================================================
   QUATERNION - Rotation representation
   ============================================================================
 */

typedef struct {
    float x, y, z, w;  /* (x,y,z) = imaginary, w = real part */
} Quat;

static inline Quat quat_identity(void) {
    return (Quat){0, 0, 0, 1};
}

static inline Quat quat_make(float x, float y, float z, float w) {
    return (Quat){x, y, z, w};
}

/* Convert Euler angles (in radians) to Quaternion */
Quat quat_from_euler(float pitch, float yaw, float roll);

/* Convert Quaternion back to Euler angles (in radians) */
void quat_to_euler(Quat q, float *pitch, float *yaw, float *roll);

/* Create quaternion from axis-angle */
Quat quat_from_axis_angle(Vec3 axis, float angle);

/* Normalize quaternion */
Quat quat_normalize(Quat q);

/* Conjugate (inverse for unit quaternions) */
Quat quat_conjugate(Quat q);

/* Quaternion multiplication (composition of rotations) */
Quat quat_multiply(Quat a, Quat b);

/* Rotate vector by quaternion */
Vec3 quat_rotate_vec3(Quat q, Vec3 v);

/* Spherical interpolation */
Quat quat_slerp(Quat a, Quat b, float t);

/* ============================================================================
   MAT4 - 4x4 Matrix (Column-Major, OpenGL convention)
   ============================================================================
 */

typedef struct {
    float m[16];  /* Column-major: m[col*4 + row] */
} Mat4;

/* Create identity matrix */
static inline Mat4 mat4_identity(void) {
    Mat4 result = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    return result;
}

/* Transform a point by a 4x4 matrix (column-major), assuming w=1 */
static inline Vec3 mat4_transform_point(Mat4 m, Vec3 p) {
    Vec3 r;
    r.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    r.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    r.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return r;
}

/* Create zero matrix */
static inline Mat4 mat4_zero(void) {
    Mat4 result = {{0}};
    return result;
}

/* Matrix multiplication: result = a * b (column-major) */
Mat4 mat4_multiply(Mat4 a, Mat4 b);

/* Multiply matrix by vector: result = mat * vec (homogeneous) */
Vec4 mat4_mul_vec4(Mat4 m, Vec4 v);

/* Multiply matrix by Vec3 (assumes w=1) */
Vec3 mat4_mul_vec3(Mat4 m, Vec3 v);

/* Matrix transpose */
Mat4 mat4_transpose(Mat4 m);

/* Matrix inverse (using Gaussian elimination) */
Mat4 mat4_inverse(Mat4 m);

/* Create translation matrix */
Mat4 mat4_translate(float x, float y, float z);

/* Create rotation matrix from quaternion */
Mat4 mat4_from_quat(Quat q);

/* Create rotation matrix from Euler angles (in radians) */
Mat4 mat4_rotate_euler(float pitch, float yaw, float roll);

/* Create rotation matrix around arbitrary axis */
Mat4 mat4_rotate_axis_angle(Vec3 axis, float angle);

/* Create scale matrix */
Mat4 mat4_scale(float x, float y, float z);

/* Create TRS matrix (Translation * Rotation * Scale) */
Mat4 mat4_trs(Vec3 translation, Quat rotation, Vec3 scale);

/* Extract scale from matrix */
Vec3 mat4_get_scale(Mat4 m);

/* Extract translation from matrix */
Vec3 mat4_get_translation(Mat4 m);

/* Extract rotation as quaternion from matrix */
Quat mat4_get_rotation(Mat4 m);

/* ============================================================================
   VIEW & PROJECTION MATRICES
   ============================================================================
 */

/* Create view matrix (LookAt) */
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);

/* Create perspective projection matrix */
Mat4 mat4_perspective(float fov_degrees, float aspect, float near, float far);

/* Create orthographic projection matrix */
Mat4 mat4_ortho(float left, float right, float bottom, float top, float near,
                float far);

/* ============================================================================
   UTILITY FUNCTIONS
   ============================================================================
 */

/* Degrees to radians */
static inline float deg_to_rad(float degrees) {
    return degrees * G3D_DEG_TO_RAD;
}

/* Radians to degrees */
static inline float rad_to_deg(float radians) {
    return radians * G3D_RAD_TO_DEG;
}

/* Clamp value */
static inline float clamp(float value, float min, float max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

/* Linear interpolation */
static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_MATH_H */
