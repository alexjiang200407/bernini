#ifndef GFX_H
#define GFX_H

#include <gfx/ffi/camera.h>
#include <gfx/ffi/common.h>
#include <gfx/ffi/material.h>
#include <gfx/ffi/mesh.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	GFX_API GfxErrorInfo
	getLastError();

	enum LogLevel
	{
		LOG_LEVEL_TRACE    = 0,
		LOG_LEVEL_DEBUG    = 1,
		LOG_LEVEL_INFO     = 2,
		LOG_LEVEL_WARN     = 3,
		LOG_LEVEL_ERROR    = 4,
		LOG_LEVEL_CRITICAL = 5,
		LOG_LEVEL_OFF      = 6,
	};

	GFX_API GfxResult
	initializeGfx(LogLevel level);

	GFX_API bool
	isGfxInitialized();

	struct WindowHandle
	{
#ifdef _WIN32
		void* hwnd;
#elif __APPLE__
	void* nsView;
#elif __linux__
	unsigned long x11Window;
#endif
	};

	struct GfxOptions
	{
		WindowHandle wnd;
		int          width;
		int          height;
		bool         headless;
		bool         enableDebugLayer;
		bool         enableGPUValidationLayer;
		bool         enablePixDebug;
	};

	/// <summary>
	/// Creates a graphics object with the given options.
	/// </summary>
	/// <param name="options">Options for graphics</param>
	/// <param name="out">Output graphics null if invalid</param>
	/// <returns>Error code</returns>
	GFX_API GfxResult
	createGraphics(GfxOptions options, Gfx* out);

	/// <summary>
	/// Draws a single frame using the given graphics. Remember to submit jobs before calling this.
	/// </summary>
	/// <param name="gfx">The graphics object</param>
	/// <returns>Error code</returns>
	GFX_API GfxResult
	drawFrame(Gfx gfx, GfxCamera cam);

#ifdef __cplusplus
}
#endif
#endif
