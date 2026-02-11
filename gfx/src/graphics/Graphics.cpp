#include "graphics/Graphics.h"
#include "camera/Camera.h"
#include "ffi/util.h"
#include "scene/Scene.h"
#include <gfx/ffi/gfx.h>

GfxResult
drawFrame(Gfx graphics, GfxScene scene, GfxCamera camera)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto* gfx_ = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);

		if (!gfx_)
			return gfx::getLastResult();

		auto* camera_ = gfx::ffi::gfxObjCast<gfx::Camera>(camera);

		if (!camera_)
			return gfx::getLastResult();

		auto* scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);

		if (!scene_)
			return gfx::getLastResult();

		gfx_->DrawFrame(*camera_, scene_->GetData());
		return GFX_RESULT_OK;
	});
}

GfxResult
createGraphics(GfxOptions options, Gfx* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		if (!gfx::ffi::validatePtr(out, "out"))
			return gfx::getLastResult();

		out->destroy = gfx::ffi::deleteThunk;

		out->ptr = gfx::IGraphics::Create(options);
		return GFX_RESULT_OK;
	});
}
