/*
 * libmod_3d_scene.h - Scene Management
 *
 * Manages collections of entities, lights, and cameras
 */

#ifndef __LIBMOD_3D_SCENE_H
#define __LIBMOD_3D_SCENE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;
    char name[64];
    int active;
    uint32_t entity_count;
    uint32_t light_count;
    int *entity_ids;
    int *light_ids;
} G3DScene;

/* Create/destroy scenes */
int g3d_scene_impl_create(const char *name);
int g3d_scene_impl_destroy(int scene_id);
int g3d_scene_impl_set_active(int scene_id);
int g3d_scene_impl_get_active(void);

/* Entity management within scene */
int g3d_scene_impl_add_entity(int scene_id, int entity_id);
int g3d_scene_impl_remove_entity(int scene_id, int entity_id);

/* Light management within scene */
int g3d_scene_impl_add_light(int scene_id, int light_id);
int g3d_scene_impl_remove_light(int scene_id, int light_id);

/* Get scene */
G3DScene *g3d_scene_impl_get(int scene_id);

/* Get all entities in active scene */
int *g3d_scene_impl_get_entities(int *count);

/* Get all lights in active scene */
int *g3d_scene_impl_get_lights(int *count);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SCENE_H */
