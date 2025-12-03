#ifndef BERNINI_RENDERER_H
#define BERNINI_RENDERER_H

#include <gfx/gfx.h>

struct Bernini_WindowHandle
{
#ifdef _WIN32
	void* hwnd;
#elif __APPLE__
	void* nsView;
#elif __linux__
	unsigned long x11Window;
#endif
};

struct Bernini_RendererOptions
{
	Bernini_WindowHandle wnd;
	int                  width  = 0;
	int                  height = 0;
};

#ifdef __cplusplus
extern "C"
{
#endif

	BERNINI_GFX_API Bernini_GfxResult
	bernini_createRenderer(Bernini_RendererOptions options, Bernini_GfxObj* out);

	BERNINI_GFX_API Bernini_GfxResult
	bernini_drawFrame(Bernini_GfxObj renderer);

#ifdef __cplusplus
}
#endif

#endif
