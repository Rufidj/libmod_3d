/*
 * libmod_3d_obj.h - Wavefront .obj loader (static meshes + .mtl base-colour textures).
 */
#ifndef __LIBMOD_3D_OBJ_H
#define __LIBMOD_3D_OBJ_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load a .obj (with its .mtl for textures) into a static G3DModel. NULL on failure. */
G3DModel *g3d_obj_load(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif
