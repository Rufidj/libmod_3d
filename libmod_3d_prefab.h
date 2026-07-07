/*
 * libmod_3d_prefab.h - Prefabs: composable groups of pieces (walls, rooms...)
 *
 * A prefab is a named list of pieces (boxes for now), each with a local
 * transform, colour, optional texture and collider flag. Prefabs can be saved
 * to / loaded from a text file and instantiated into a scene at a world
 * position + yaw (the editor's "place with the mouse"). This is the building
 * block for modelling simple geometry and reusing it across a map.
 */

#ifndef __LIBMOD_3D_PREFAB_H
#define __LIBMOD_3D_PREFAB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create an empty prefab; returns its id or -1. */
int g3d_prefab_create(const char *name);

/* Add a box piece: local position, size, rotation (degrees), colour, collider. */
int g3d_prefab_add_box(int prefab, float px, float py, float pz,
                       float sx, float sy, float sz,
                       float rx, float ry, float rz,
                       float r, float g, float b, int collider);

/* Give the LAST added piece a texture (tileable wall/floor look). */
void g3d_prefab_piece_texture(int prefab, const char *path);

/* Number of pieces. */
int g3d_prefab_count(int prefab);

/* Save / load a prefab to a text file. load returns a new prefab id. */
int g3d_prefab_save(int prefab, const char *file);
int g3d_prefab_load(const char *file);

/* Instantiate the prefab into a scene at world (x,y,z) rotated yaw degrees.
   Spawns one entity per piece (colliders flagged). Returns pieces spawned. */
int g3d_prefab_instantiate(int prefab, int scene, float x, float y, float z,
                           float yaw_deg);

void g3d_prefab_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PREFAB_H */
