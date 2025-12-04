#ifndef GFX_COMMON_H
#define GFX_COMMON_H
#include <stdint.h>
typedef uint64_t GfxID;

struct GfxObj
{
	void (*destroy)(GfxObj self);
	void* data;
};

enum GfxResult
{
	GFX_RESULT_OK                     = 0,
	GFX_RESULT_ERROR_UNKNOWN          = 1,
	GFX_RESULT_ERROR_INVALID_ARGUMENT = 2,
	GFX_RESULT_ERROR_DIRECTX11_ERROR  = 3,
	GFX_RESULT_ERROR_INVALID_HANDLE   = 4,
	GFX_RESULT_ERROR_OUT_OF_HANDLES   = 5,
	GFX_RESULT_ERROR_OUT_OF_MEMORY    = 6,
	GFX_RESULT_ERROR_NOT_INITIALIZED  = 7,
};

struct GfxErrorInfo
{
	GfxResult result;
	char      title[256];
	char      message[512];
};

#ifdef GFX_EXPORTS
#	define GFX_API __declspec(dllexport)
#else
#	define GFX_API __declspec(dllimport)
#endif

typedef GfxObj Graphics;
typedef GfxObj Camera;

#endif
