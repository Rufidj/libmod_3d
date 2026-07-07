/*
 * libmod_3d_scene.c - Scene Management Implementation
 */

#include "libmod_3d_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static G3DScene g_scenes[16];
static int g_scene_count = 0;
static int g_active_scene_id = -1;

int g3d_scene_impl_create(const char *name) {
    if (g_scene_count >= 16) {
        fprintf(stderr, "G3D: Max scenes reached\n");
        return -1;
    }

    int scene_id = g_scene_count++;
    G3DScene *scene = &g_scenes[scene_id];

    memset(scene, 0, sizeof(G3DScene));
    scene->id = scene_id;
    strncpy(scene->name, name, 63);
    scene->name[63] = '\0';

    scene->entity_ids = (int *)calloc(4096, sizeof(int));
    scene->light_ids = (int *)calloc(32, sizeof(int));

    if (!scene->entity_ids || !scene->light_ids) {
        free(scene->entity_ids);
        free(scene->light_ids);
        fprintf(stderr, "G3D: Failed to allocate scene arrays\n");
        return -1;
    }

    /* Initialize IDs to -1 (invalid) */
    for (uint32_t i = 0; i < 4096; i++)
        scene->entity_ids[i] = -1;
    for (uint32_t i = 0; i < 32; i++)
        scene->light_ids[i] = -1;

    printf("G3D: Scene created: %s (id=%d)\n", name, scene_id);
    return scene_id;
}

int g3d_scene_impl_destroy(int scene_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    G3DScene *scene = &g_scenes[scene_id];

    if (scene->entity_ids)
        free(scene->entity_ids);
    if (scene->light_ids)
        free(scene->light_ids);

    if (g_active_scene_id == scene_id)
        g_active_scene_id = -1;

    printf("G3D: Scene destroyed: %s\n", scene->name);
    return 1;
}

int g3d_scene_impl_set_active(int scene_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    g_active_scene_id = scene_id;
    printf("G3D: Active scene set to: %s\n", g_scenes[scene_id].name);
    return 1;
}

int g3d_scene_impl_get_active(void) {
    return g_active_scene_id;
}

int g3d_scene_impl_add_entity(int scene_id, int entity_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    G3DScene *scene = &g_scenes[scene_id];

    if (scene->entity_count >= 4096)
        return 0;

    scene->entity_ids[scene->entity_count++] = entity_id;
    return 1;
}

int g3d_scene_impl_remove_entity(int scene_id, int entity_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    G3DScene *scene = &g_scenes[scene_id];

    for (uint32_t i = 0; i < scene->entity_count; i++) {
        if (scene->entity_ids[i] == entity_id) {
            /* Swap with last and shrink */
            scene->entity_ids[i] = scene->entity_ids[scene->entity_count - 1];
            scene->entity_ids[scene->entity_count - 1] = -1;
            scene->entity_count--;
            return 1;
        }
    }

    return 0;
}

int g3d_scene_impl_add_light(int scene_id, int light_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    G3DScene *scene = &g_scenes[scene_id];

    if (scene->light_count >= 32)
        return 0;

    scene->light_ids[scene->light_count++] = light_id;
    return 1;
}

int g3d_scene_impl_remove_light(int scene_id, int light_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return 0;

    G3DScene *scene = &g_scenes[scene_id];

    for (uint32_t i = 0; i < scene->light_count; i++) {
        if (scene->light_ids[i] == light_id) {
            scene->light_ids[i] = scene->light_ids[scene->light_count - 1];
            scene->light_ids[scene->light_count - 1] = -1;
            scene->light_count--;
            return 1;
        }
    }

    return 0;
}

G3DScene *g3d_scene_impl_get(int scene_id) {
    if (scene_id < 0 || scene_id >= g_scene_count)
        return NULL;
    return &g_scenes[scene_id];
}

int *g3d_scene_impl_get_entities(int *count) {
    if (g_active_scene_id < 0 || g_active_scene_id >= g_scene_count) {
        if (count)
            *count = 0;
        return NULL;
    }

    G3DScene *scene = &g_scenes[g_active_scene_id];
    if (count)
        *count = scene->entity_count;
    return scene->entity_ids;
}

int *g3d_scene_impl_get_lights(int *count) {
    if (g_active_scene_id < 0 || g_active_scene_id >= g_scene_count) {
        if (count)
            *count = 0;
        return NULL;
    }

    G3DScene *scene = &g_scenes[g_active_scene_id];
    if (count)
        *count = scene->light_count;
    return scene->light_ids;
}
