/*
 * libmod_3d_fbx.h - FBX loader via ufbx (static + skinned + animations, e.g. Mixamo).
 */
#ifndef __LIBMOD_3D_FBX_H
#define __LIBMOD_3D_FBX_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load an .fbx into a G3DModel (meshes + textures + skeleton + baked animations).
   NULL on failure. Works with Mixamo character+animation exports. */
G3DModel *g3d_fbx_load(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif
