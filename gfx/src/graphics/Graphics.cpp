#include "graphics/Graphics.h"
#include "camera/Camera.h"
#include "ffi/util.h"
#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"
#include <gfx/ffi/gfx.h>

GfxResult
drawFrame(Gfx graphics, GfxCamera camera)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& gfx_ = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);
		auto& cam  = gfx::ffi::gfxObjCast<gfx::Camera>(camera);
		gfx_.DrawFrame(cam);
		return GFX_RESULT_OK;
	});
}

GfxResult
createGraphics(GfxOptions options, Gfx* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");
		out->destroy = gfx::ffi::deleteThunk;

		out->ptr = gfx::IGraphics::Create(options);
		return GFX_RESULT_OK;
	});
}

GfxResult
createCube(Gfx graphics, GfxMat4 modelTransform, GfxMesh* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& gfx_ = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);
		gfx::ffi::validatePtr(out, "out");

		auto& factory = gfx_.GetMeshFactory();
		*out          = factory.CreateCubeInstance(glm::make_mat4(modelTransform));

		return GFX_RESULT_OK;
	});
}

GFX_API GfxResult
destroyMesh(Gfx graphics, GfxMesh mesh)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& gfx_     = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);
		auto& registry = gfx_.GetMeshRegistry();
		registry.RemoveMeshInstance(mesh);
		return GFX_RESULT_OK;
	});
}
