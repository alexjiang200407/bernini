#ifndef GFX_COMMON_H
#define GFX_COMMON_H

#include <stdint.h>
typedef uint64_t GfxID;

struct GfxObj
{
	void (*destroy)(GfxObj self);
	void* data;
};

#pragma warning(push)
#pragma warning(disable: 4201)

typedef union
{
	float xyz[3];
	struct
	{
		float x, y, z;
	};
	struct
	{
		float pitch, yaw, roll;
	};
} GfxVec3;

#pragma warning(pop)

enum GfxResult
{
	GFX_RESULT_OK = 0,
	GFX_RESULT_ERROR_UNKNOWN,
	GFX_RESULT_ERROR_INVALID_ARGUMENT,
	GFX_RESULT_ERROR_DIRECTX11_ERROR,
	GFX_RESULT_ERROR_DIRECTX12_ERROR,
	GFX_RESULT_ERROR_INVALID_HANDLE,
	GFX_RESULT_ERROR_OUT_OF_HANDLES,
	GFX_RESULT_ERROR_OUT_OF_MEMORY,
	GFX_RESULT_ERROR_NOT_INITIALIZED,
	GFX_RESULT_ERROR_SHADER_REFLECT,
	GFX_RESULT_DYNAMIC_BUFFER,
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

typedef GfxObj Gfx;
typedef GfxObj GfxCamera;

#endif
