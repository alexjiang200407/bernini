#ifndef MESH_H
#define MESH_H

#include <gfx/ffi/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

	GFX_API GfxResult
	createCube(Gfx graphics, GfxMat4 modelTransform, GfxMesh* out);

	GFX_API GfxResult
	destroyMesh(Gfx graphics, GfxMesh mesh);

#ifdef __cplusplus
}
#endif

#endif  // !MESH_H
