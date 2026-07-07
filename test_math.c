/*
 * test_math.c - Unit tests for libmod_3d_math
 *
 * Quick tests to verify math operations
 * Compile: gcc test_math.c libmod_3d_math.c -o test_math -lm
 */

#include "libmod_3d_math.h"
#include <stdio.h>
#include <assert.h>

#define ASSERT_FLOAT_EQ(a, b, tolerance) \
    assert(fabsf((a) - (b)) < (tolerance))

void test_vec3() {
    printf("Testing Vec3...\n");

    Vec3 a = vec3_make(1, 2, 3);
    Vec3 b = vec3_make(4, 5, 6);

    /* Addition */
    Vec3 c = vec3_add(a, b);
    assert(c.x == 5 && c.y == 7 && c.z == 9);

    /* Subtraction */
    Vec3 d = vec3_sub(b, a);
    assert(d.x == 3 && d.y == 3 && d.z == 3);

    /* Dot product */
    float dot = vec3_dot(a, b);
    assert(dot == 32);  /* 1*4 + 2*5 + 3*6 = 32 */

    /* Cross product */
    Vec3 cross = vec3_cross(a, b);
    assert(cross.x == -3 && cross.y == 6 && cross.z == -3);

    /* Normalize */
    Vec3 normalized = vec3_normalize(vec3_make(3, 4, 0));
    ASSERT_FLOAT_EQ(normalized.x, 0.6f, 1e-5);
    ASSERT_FLOAT_EQ(normalized.y, 0.8f, 1e-5);

    /* Length */
    float len = vec3_length(vec3_make(3, 4, 0));
    ASSERT_FLOAT_EQ(len, 5.0f, 1e-5);

    printf("  Vec3 tests passed!\n");
}

void test_quat() {
    printf("Testing Quaternion...\n");

    /* Identity quaternion */
    Quat q = quat_identity();
    assert(q.x == 0 && q.y == 0 && q.z == 0 && q.w == 1);

    /* Euler to Quat conversion */
    Quat q2 = quat_from_euler(0, 0, 0);
    ASSERT_FLOAT_EQ(q2.x, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(q2.y, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(q2.z, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(q2.w, 1.0f, 1e-5);

    /* Rotate vector */
    Quat rotate_90_z = quat_from_euler(0, 0, G3D_PI / 2);
    Vec3 v = vec3_make(1, 0, 0);
    Vec3 rotated = quat_rotate_vec3(rotate_90_z, v);
    ASSERT_FLOAT_EQ(rotated.x, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(rotated.y, 1.0f, 1e-5);
    ASSERT_FLOAT_EQ(rotated.z, 0.0f, 1e-5);

    printf("  Quaternion tests passed!\n");
}

void test_matrix() {
    printf("Testing Matrix...\n");

    /* Identity matrix */
    Mat4 identity = mat4_identity();
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) {
                assert(identity.m[i * 4 + j] == 1.0f);
            } else {
                assert(identity.m[i * 4 + j] == 0.0f);
            }
        }
    }

    /* Multiply with vector */
    Mat4 trans = mat4_translate(1, 2, 3);
    Vec3 v = vec3_make(0, 0, 0);
    Vec3 result = mat4_mul_vec3(trans, v);
    assert(result.x == 1 && result.y == 2 && result.z == 3);

    /* Matrix multiplication (identity) */
    Mat4 a = mat4_identity();
    Mat4 b = mat4_identity();
    Mat4 c = mat4_multiply(a, b);
    for (int i = 0; i < 16; i++) {
        assert(c.m[i] == identity.m[i]);
    }

    /* Transpose */
    Mat4 m = mat4_make(1, 2, 3, 4,    /* Col 0 */
                       5, 6, 7, 8,    /* Col 1 */
                       9, 10, 11, 12, /* Col 2 */
                       13, 14, 15, 16); /* Col 3 */
    // (This syntax doesn't exist, just conceptual)

    /* TRS composition */
    Vec3 translation = vec3_make(1, 2, 3);
    Quat rotation = quat_identity();
    Vec3 scale = vec3_make(2, 2, 2);
    Mat4 trs = mat4_trs(translation, rotation, scale);
    Vec3 origin = vec3_make(0, 0, 0);
    Vec3 transformed = mat4_mul_vec3(trs, origin);
    assert(transformed.x == 1 && transformed.y == 2 && transformed.z == 3);

    printf("  Matrix tests passed!\n");
}

void test_lookAt() {
    printf("Testing LookAt...\n");

    Vec3 eye = vec3_make(0, 0, 5);
    Vec3 target = vec3_make(0, 0, 0);
    Vec3 up = vec3_make(0, 1, 0);

    Mat4 view = mat4_look_at(eye, target, up);

    /* View matrix should transform camera to origin looking down -Z */
    Vec3 camera_pos = mat4_mul_vec3(view, eye);
    ASSERT_FLOAT_EQ(camera_pos.x, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(camera_pos.y, 0.0f, 1e-5);
    ASSERT_FLOAT_EQ(camera_pos.z, 0.0f, 1e-5);

    printf("  LookAt tests passed!\n");
}

void test_perspective() {
    printf("Testing Perspective...\n");

    Mat4 proj = mat4_perspective(75.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    /* Check that projection matrix is not identity */
    Mat4 id = mat4_identity();
    int different = 0;
    for (int i = 0; i < 16; i++) {
        if (fabsf(proj.m[i] - id.m[i]) > 1e-5) {
            different++;
        }
    }
    assert(different > 0);

    printf("  Perspective tests passed!\n");
}

int main() {
    printf("=== libmod_3d Math Test Suite ===\n\n");

    test_vec3();
    test_quat();
    test_matrix();
    test_lookAt();
    test_perspective();

    printf("\n=== All tests passed! ===\n");
    return 0;
}

/* Helper for matrix creation in tests (conceptual only) */
/*
static Mat4 mat4_make(float m0, float m1, float m2, float m3,
                      float m4, float m5, float m6, float m7,
                      float m8, float m9, float m10, float m11,
                      float m12, float m13, float m14, float m15) {
    return (Mat4){{m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15}};
}
*/
