#ifndef MESH_H
#define MESH_H

#include <gfx/ffi/common.h>

#ifdef __cplusplus
extern "C"
{
#endif
	GFX_API GfxResult
	createCubeBase(GfxScene scene, GfxStaticMesh* out);

	GFX_API GfxResult
	createSphereBase(GfxScene scene, GfxStaticMesh* out);

	struct GfxStaticMeshOpts
	{
		GfxStaticMesh baseMesh;
		GfxMaterial   material;
		GfxMat4       modelTransform;
	};

	GFX_API GfxResult
	createStaticMeshInstance(GfxScene scene, GfxStaticMeshOpts data, GfxMeshInstance* out);

	GFX_API GfxResult
	destroyMeshInstance(GfxScene scene, GfxMeshInstance meshInstance);

	GFX_API GfxResult
	destroyStaticMesh(GfxScene scene, GfxStaticMesh mesh);

#ifdef __cplusplus
}
#endif

#endif  // !MESH_H
