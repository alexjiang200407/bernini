#include "graphics/Graphics.h"
#include "camera/Camera.h"
#include "ffi/util.h"
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

		out->data = gfx::IGraphics::Create(options);
		return GFX_RESULT_OK;
	});
}
