/*
 * libmod_3d_light.h - Lighting Management
 *
 * Manages directional, point, and spot lights with shadow mapping
 */

#ifndef __LIBMOD_3D_LIGHT_H
#define __LIBMOD_3D_LIGHT_H

#include "libmod_3d_math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Light type values (matches #define in libmod_3d.h) */
#define G3D_LIGHT_TYPE_DIRECTIONAL 0
#define G3D_LIGHT_TYPE_POINT 1
#define G3D_LIGHT_TYPE_SPOT 2

typedef struct {
    int id;
    int type;
    int active;

    /* Position/Direction */
    Vec3 position;
    Vec3 direction;

    /* Color and intensity */
    float color[3];  /* RGB */
    float intensity;

    /* Range (for point/spot lights) */
    float range;

    /* Cone angle (for spot lights) */
    float cone_angle;

    /* Shadows */
    int cast_shadows;
    int shadow_resolution;
    uint32_t shadow_map_handle;
} G3DLight;

/* Light lifecycle */
int g3d_light_impl_create(int type, float r, float g, float b);
int g3d_light_impl_destroy(int light_id);
G3DLight *g3d_light_impl_get(int light_id);

/* Light properties */
int g3d_light_impl_set_position(int light_id, float x, float y, float z);
int g3d_light_impl_set_direction(int light_id, float dx, float dy, float dz);
int g3d_light_impl_set_color(int light_id, float r, float g, float b);
int g3d_light_impl_set_intensity(int light_id, float intensity);
int g3d_light_impl_set_range(int light_id, float range);
int g3d_light_impl_set_cone(int light_id, float angle_degrees);
int g3d_light_impl_enable_shadow(int light_id, int enabled);
int g3d_light_impl_set_shadow_quality(int light_id, int resolution);

/* Get all lights */
int *g3d_light_impl_get_all(int *count);

/* Cleanup */
void g3d_light_impl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_LIGHT_H */
