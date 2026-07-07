/*
 * libmod_3d_cloth.h - Cloth simulation (flags, hanging sheets) via Verlet
 *
 * A cloth is a grid of particles linked by distance constraints, integrated with
 * Verlet and relaxed each frame. Pin an edge (hang/flag), blow wind on it, and
 * let a moving sphere (the character) push it. The result is a normal G3DMesh in
 * the active scene, so it gets textured/lit/shadowed like everything else.
 */

#ifndef __LIBMOD_3D_CLOTH_H
#define __LIBMOD_3D_CLOTH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create a cloth (width x height world units, nx*ny particles) with its top-left
   corner at (px,py,pz), flat in the XY plane hanging toward -Y. Spawns an entity
   in the active scene. Returns a cloth id, or -1. */
int g3d_cloth_create(float width, float height, int nx, int ny,
                     float px, float py, float pz);

/* Pin mode: 0 = top edge, 1 = top corners, 2 = left edge (flag). */
void g3d_cloth_pin(int cloth, int mode);

void g3d_cloth_set_wind(int cloth, float x, float y, float z, float strength);
void g3d_cloth_set_collider(int cloth, float x, float y, float z, float radius);
void g3d_cloth_clear_collider(int cloth);
void g3d_cloth_set_texture(int cloth, unsigned int gl_handle);

/* Step the simulation by dt seconds and re-upload the mesh. */
void g3d_cloth_update(int cloth, float dt);

void g3d_cloth_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_CLOTH_H */
