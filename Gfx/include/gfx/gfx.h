#ifndef BERNINI_GFX_H
#define BERNINI_GFX_H

#ifdef GFX_EXPORTS
#	define BERNINI_GFX_API __declspec(dllexport)
#else
#	define BERNINI_GFX_API __declspec(dllimport)
#endif

struct Bernini_GfxObj
{
	void (*destroy)(struct Bernini_GfxObj self);
	void* data;
};

#define BERNINI_GFX_OBJ_DEFAULT (Bernini_GfxObj){ 0 }

enum Bernini_GfxResult
{
	BERNINI_GFX_RENDERER_RESULT_OK                     = 0,
	BERNINI_GFX_RENDERER_RESULT_ERROR_UNKNOWN          = 1,
	BERNINI_GFX_RENDERER_RESULT_ERROR_INVALID_ARGUMENT = 2,
	BERNINI_GFX_RENDERER_RESULT_ERROR_DIRECTX11_ERROR  = 3,
	BERNINI_GFX_RENDERER_RESULT_ERROR_INVALID_HANDLE   = 4,
};

#include <gfx/Renderer.h>

#ifdef __cplusplus
extern "C"
{
#endif

	struct Bernini_GfxErrorInfo
	{
		Bernini_GfxResult result;
		char              title[256];
		char              message[512];
	};

	BERNINI_GFX_API Bernini_GfxErrorInfo
	bernini_getLastError();

#ifdef __cplusplus
}
#endif

#endif
