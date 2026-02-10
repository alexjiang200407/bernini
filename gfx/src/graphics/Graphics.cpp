#include "graphics/Graphics.h"
#include "camera/Camera.h"
#include "ffi/util.h"
#include "scene/Scene.h"
#include <gfx/ffi/gfx.h>

GfxResult
drawFrame(Gfx graphics, GfxScene scene, GfxCamera camera)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& gfx_    = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);
		auto& camera_ = gfx::ffi::gfxObjCast<gfx::Camera>(camera);
		auto& scene_  = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx_.DrawFrame(camera_, scene_.GetData());
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
