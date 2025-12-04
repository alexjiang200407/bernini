#include "graphics/Graphics.h"
#include "ffi/util.h"
#include <gfx/gfx.h>

GfxResult
drawFrame(Graphics graphics)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& ren = gfx::ffi::gfxObjCast<gfx::IGraphics>(graphics);
		ren.DrawFrame();
		return GFX_RESULT_OK;
	});
}
