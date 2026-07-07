/*
 * libmod_3d_math.c - Vector and Matrix Math Implementation
 *
 * Core mathematical operations for 3D graphics
 */

#include "libmod_3d_math.h"
#include <math.h>
#include <string.h>

/* ============================================================================
   QUATERNION IMPLEMENTATIONS
   ============================================================================
 */

Quat quat_from_euler(float pitch, float yaw, float roll) {
    /* Convert Euler angles to Quaternion
     * Order: Yaw (Y) * Pitch (X) * Roll (Z)
     * This is ZYX Euler angle convention
     */
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);

    Quat q;
    q.w = cy * cp * cr + sy * sp * sr;
    q.x = cy * sp * cr + sy * cp * sr;
    q.y = sy * cp * cr - cy * sp * sr;
    q.z = cy * cp * sr - sy * sp * cr;

    return q;
}

void quat_to_euler(Quat q, float *pitch, float *yaw, float *roll) {
    /* Convert Quaternion back to Euler angles */
    float sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
    *roll = atan2f(sinr_cosp, cosr_cosp);

    float sinp = 2 * (q.w * q.y - q.z * q.x);
    if (fabsf(sinp) >= 1)
        *pitch = copysignf(G3D_PI / 2, sinp);
    else
        *pitch = asinf(sinp);

    float siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    *yaw = atan2f(siny_cosp, cosy_cosp);
}

Quat quat_from_axis_angle(Vec3 axis, float angle) {
    axis = vec3_normalize(axis);
    float half_angle = angle * 0.5f;
    float sin_half = sinf(half_angle);

    return (Quat){axis.x * sin_half, axis.y * sin_half, axis.z * sin_half,
                  cosf(half_angle)};
}

Quat quat_normalize(Quat q) {
    float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < G3D_EPSILON)
        return quat_identity();
    return (Quat){q.x / len, q.y / len, q.z / len, q.w / len};
}

Quat quat_conjugate(Quat q) {
    return (Quat){-q.x, -q.y, -q.z, q.w};
}

Quat quat_multiply(Quat a, Quat b) {
    /* Quaternion multiplication (composition) */
    return (Quat){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

Vec3 quat_rotate_vec3(Quat q, Vec3 v) {
    /* Rotate vector using quaternion: q * v * q^-1 */
    Quat qv = (Quat){v.x, v.y, v.z, 0};
    Quat q_conj = quat_conjugate(q);

    Quat result = quat_multiply(quat_multiply(q, qv), q_conj);
    return (Vec3){result.x, result.y, result.z};
}

Quat quat_slerp(Quat a, Quat b, float t) {
    /* Spherical linear interpolation between two quaternions */
    Quat result;

    float dot_product =
        a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    if (dot_product < 0) {
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
        dot_product = -dot_product;
    }

    dot_product = clamp(dot_product, -1.0f, 1.0f);
    float theta = acosf(dot_product);
    float sin_theta = sinf(theta);

    if (sin_theta < G3D_EPSILON) {
        result.x = a.x + (b.x - a.x) * t;
        result.y = a.y + (b.y - a.y) * t;
        result.z = a.z + (b.z - a.z) * t;
        result.w = a.w + (b.w - a.w) * t;
    } else {
        float w0 = sinf((1 - t) * theta) / sin_theta;
        float w1 = sinf(t * theta) / sin_theta;

        result.x = a.x * w0 + b.x * w1;
        result.y = a.y * w0 + b.y * w1;
        result.z = a.z * w0 + b.z * w1;
        result.w = a.w * w0 + b.w * w1;
    }

    return quat_normalize(result);
}

/* ============================================================================
   MATRIX IMPLEMENTATIONS
   ============================================================================
 */

Mat4 mat4_multiply(Mat4 a, Mat4 b) {
    Mat4 result = mat4_zero();

    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }

    return result;
}

Vec4 mat4_mul_vec4(Mat4 m, Vec4 v) {
    Vec4 result;
    result.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    result.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    result.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    result.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return result;
}

Vec3 mat4_mul_vec3(Mat4 m, Vec3 v) {
    Vec4 v4 = vec4_from_vec3(v, 1.0f);
    Vec4 result = mat4_mul_vec4(m, v4);
    if (result.w != 0.0f) {
        return (Vec3){result.x / result.w, result.y / result.w,
                      result.z / result.w};
    }
    return (Vec3){result.x, result.y, result.z};
}

Mat4 mat4_transpose(Mat4 m) {
    Mat4 result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[j * 4 + i] = m.m[i * 4 + j];
        }
    }
    return result;
}

/* Calculate determinant of 3x3 submatrix */
static float mat3_determinant(float m[9]) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) -
           m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

Mat4 mat4_inverse(Mat4 m) {
    /* Compute inverse using cofactor method */
    Mat4 inv = mat4_zero();

    /* Calculate all 2x2 determinants */
    float det2[36];
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                for (int l = 0; l < 4; l++) {
                    if (k != i && l != j) {
                        /* Extract 3x3 minor and compute determinant */
                    }
                }
            }
        }
    }

    /* Simplified: Use standard 4x4 inverse formula */
    float A2323 = m.m[10] * m.m[15] - m.m[14] * m.m[11];
    float A1323 = m.m[9] * m.m[15] - m.m[13] * m.m[11];
    float A1223 = m.m[9] * m.m[14] - m.m[13] * m.m[10];
    float A0323 = m.m[8] * m.m[15] - m.m[12] * m.m[11];
    float A0223 = m.m[8] * m.m[14] - m.m[12] * m.m[10];
    float A0123 = m.m[8] * m.m[13] - m.m[12] * m.m[9];
    float A2313 = m.m[6] * m.m[15] - m.m[14] * m.m[7];
    float A1313 = m.m[5] * m.m[15] - m.m[13] * m.m[7];
    float A1213 = m.m[5] * m.m[14] - m.m[13] * m.m[6];
    float A2312 = m.m[6] * m.m[11] - m.m[10] * m.m[7];
    float A1312 = m.m[5] * m.m[11] - m.m[9] * m.m[7];
    float A1212 = m.m[5] * m.m[10] - m.m[9] * m.m[6];
    float A0313 = m.m[4] * m.m[15] - m.m[12] * m.m[7];
    float A0213 = m.m[4] * m.m[14] - m.m[12] * m.m[6];
    float A0312 = m.m[4] * m.m[11] - m.m[8] * m.m[7];
    float A0212 = m.m[4] * m.m[10] - m.m[8] * m.m[6];
    float A0113 = m.m[4] * m.m[13] - m.m[12] * m.m[5];
    float A0112 = m.m[4] * m.m[9] - m.m[8] * m.m[5];

    float det = m.m[0] * (m.m[5] * A2323 - m.m[6] * A1323 + m.m[7] * A1223) -
                m.m[1] * (m.m[4] * A2323 - m.m[6] * A0323 + m.m[7] * A0223) +
                m.m[2] * (m.m[4] * A1323 - m.m[5] * A0323 + m.m[7] * A0123) -
                m.m[3] * (m.m[4] * A1223 - m.m[5] * A0223 + m.m[6] * A0123);

    if (fabsf(det) < G3D_EPSILON) {
        return mat4_identity(); /* Singular matrix */
    }

    inv.m[0] = (m.m[5] * A2323 - m.m[6] * A1323 + m.m[7] * A1223) / det;
    inv.m[4] = -(m.m[4] * A2323 - m.m[6] * A0323 + m.m[7] * A0223) / det;
    inv.m[8] = (m.m[4] * A1323 - m.m[5] * A0323 + m.m[7] * A0123) / det;
    inv.m[12] = -(m.m[4] * A1223 - m.m[5] * A0223 + m.m[6] * A0123) / det;

    inv.m[1] = -(m.m[1] * A2323 - m.m[2] * A1323 + m.m[3] * A1223) / det;
    inv.m[5] = (m.m[0] * A2323 - m.m[2] * A0323 + m.m[3] * A0223) / det;
    inv.m[9] = -(m.m[0] * A1323 - m.m[1] * A0323 + m.m[3] * A0123) / det;
    inv.m[13] = (m.m[0] * A1223 - m.m[1] * A0223 + m.m[2] * A0123) / det;

    inv.m[2] = (m.m[1] * A2313 - m.m[2] * A1313 + m.m[3] * A1213) / det;
    inv.m[6] = -(m.m[0] * A2313 - m.m[2] * A0313 + m.m[3] * A0213) / det;
    inv.m[10] = (m.m[0] * A1313 - m.m[1] * A0313 + m.m[3] * A0113) / det;
    inv.m[14] = -(m.m[0] * A1213 - m.m[1] * A0213 + m.m[2] * A0113) / det;

    inv.m[3] = -(m.m[1] * A2312 - m.m[2] * A1312 + m.m[3] * A1212) / det;
    inv.m[7] = (m.m[0] * A2312 - m.m[2] * A0312 + m.m[3] * A0212) / det;
    inv.m[11] = -(m.m[0] * A1312 - m.m[1] * A0312 + m.m[3] * A0112) / det;
    inv.m[15] = (m.m[0] * A1212 - m.m[1] * A0212 + m.m[2] * A0112) / det;

    return inv;
}

Mat4 mat4_translate(float x, float y, float z) {
    Mat4 result = mat4_identity();
    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;
    return result;
}

Mat4 mat4_from_quat(Quat q) {
    q = quat_normalize(q);
    Mat4 result = mat4_identity();

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    result.m[0] = 1 - 2 * (yy + zz);
    result.m[1] = 2 * (xy + wz);
    result.m[2] = 2 * (xz - wy);

    result.m[4] = 2 * (xy - wz);
    result.m[5] = 1 - 2 * (xx + zz);
    result.m[6] = 2 * (yz + wx);

    result.m[8] = 2 * (xz + wy);
    result.m[9] = 2 * (yz - wx);
    result.m[10] = 1 - 2 * (xx + yy);

    return result;
}

Mat4 mat4_rotate_euler(float pitch, float yaw, float roll) {
    Quat q = quat_from_euler(pitch, yaw, roll);
    return mat4_from_quat(q);
}

Mat4 mat4_rotate_axis_angle(Vec3 axis, float angle) {
    Quat q = quat_from_axis_angle(axis, angle);
    return mat4_from_quat(q);
}

Mat4 mat4_scale(float x, float y, float z) {
    Mat4 result = mat4_identity();
    result.m[0] = x;
    result.m[5] = y;
    result.m[10] = z;
    return result;
}

Mat4 mat4_trs(Vec3 translation, Quat rotation, Vec3 scale) {
    /* Combine Translation, Rotation, Scale: T * R * S */
    Mat4 t = mat4_translate(translation.x, translation.y, translation.z);
    Mat4 r = mat4_from_quat(rotation);
    Mat4 s = mat4_scale(scale.x, scale.y, scale.z);

    Mat4 rs = mat4_multiply(r, s);
    return mat4_multiply(t, rs);
}

Vec3 mat4_get_scale(Mat4 m) {
    Vec3 sx = {m.m[0], m.m[1], m.m[2]};
    Vec3 sy = {m.m[4], m.m[5], m.m[6]};
    Vec3 sz = {m.m[8], m.m[9], m.m[10]};

    return (Vec3){vec3_length(sx), vec3_length(sy), vec3_length(sz)};
}

Vec3 mat4_get_translation(Mat4 m) {
    return (Vec3){m.m[12], m.m[13], m.m[14]};
}

Quat mat4_get_rotation(Mat4 m) {
    /* Extract rotation as quaternion from matrix */
    Vec3 scale = mat4_get_scale(m);

    Mat4 m_normalized = m;
    m_normalized.m[0] /= scale.x;
    m_normalized.m[1] /= scale.x;
    m_normalized.m[2] /= scale.x;

    m_normalized.m[4] /= scale.y;
    m_normalized.m[5] /= scale.y;
    m_normalized.m[6] /= scale.y;

    m_normalized.m[8] /= scale.z;
    m_normalized.m[9] /= scale.z;
    m_normalized.m[10] /= scale.z;

    float trace =
        m_normalized.m[0] + m_normalized.m[5] + m_normalized.m[10];

    if (trace > 0) {
        float s = sqrtf(trace + 1.0f) * 2;
        return (Quat){
            (m_normalized.m[6] - m_normalized.m[9]) / s,
            (m_normalized.m[8] - m_normalized.m[2]) / s,
            (m_normalized.m[1] - m_normalized.m[4]) / s,
            0.25f * s,
        };
    } else if (m_normalized.m[0] > m_normalized.m[5] &&
               m_normalized.m[0] > m_normalized.m[10]) {
        float s = sqrtf(1.0f + m_normalized.m[0] - m_normalized.m[5] -
                        m_normalized.m[10]) *
                  2;
        return (Quat){
            0.25f * s,
            (m_normalized.m[1] + m_normalized.m[4]) / s,
            (m_normalized.m[8] + m_normalized.m[2]) / s,
            (m_normalized.m[6] - m_normalized.m[9]) / s,
        };
    } else if (m_normalized.m[5] > m_normalized.m[10]) {
        float s =
            sqrtf(1.0f + m_normalized.m[5] - m_normalized.m[0] -
                  m_normalized.m[10]) *
            2;
        return (Quat){
            (m_normalized.m[1] + m_normalized.m[4]) / s,
            0.25f * s,
            (m_normalized.m[6] + m_normalized.m[9]) / s,
            (m_normalized.m[8] - m_normalized.m[2]) / s,
        };
    } else {
        float s = sqrtf(1.0f + m_normalized.m[10] - m_normalized.m[0] -
                        m_normalized.m[5]) *
                  2;
        return (Quat){
            (m_normalized.m[8] + m_normalized.m[2]) / s,
            (m_normalized.m[6] + m_normalized.m[9]) / s,
            0.25f * s,
            (m_normalized.m[1] - m_normalized.m[4]) / s,
        };
    }
}

/* ============================================================================
   VIEW & PROJECTION MATRICES
   ============================================================================
 */

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    /* Create view matrix using eye, target, up vectors */
    Vec3 f = vec3_normalize(vec3_sub(target, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);

    Mat4 result = mat4_identity();

    result.m[0] = s.x;
    result.m[4] = s.y;
    result.m[8] = s.z;

    result.m[1] = u.x;
    result.m[5] = u.y;
    result.m[9] = u.z;

    result.m[2] = -f.x;
    result.m[6] = -f.y;
    result.m[10] = -f.z;

    result.m[12] = -vec3_dot(s, eye);
    result.m[13] = -vec3_dot(u, eye);
    result.m[14] = vec3_dot(f, eye);

    return result;
}

Mat4 mat4_perspective(float fov_degrees, float aspect, float near, float far) {
    /* Create perspective projection matrix */
    float fov_rad = deg_to_rad(fov_degrees);
    float f = 1.0f / tanf(fov_rad / 2.0f);

    Mat4 result = mat4_zero();

    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (far + near) / (near - far);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * far * near) / (near - far);

    return result;
}

Mat4 mat4_ortho(float left, float right, float bottom, float top, float near,
                float far) {
    /* Create orthographic projection matrix */
    Mat4 result = mat4_identity();

    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = -2.0f / (far - near);

    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(far + near) / (far - near);

    return result;
}
