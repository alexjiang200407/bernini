#include "RendererException.h"
#include <gfx/Renderer.h>

Bernini_GfxErrorInfo
bernini_getLastError()
{
	return gfx::RendererException::GetLastErrorInfo();
}
