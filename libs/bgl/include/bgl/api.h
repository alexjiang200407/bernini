#pragma once

/**
 * Marks what the renderer exports when it is a DLL (BGL_SHARED, set by the build); nothing when it
 * is linked statically. See docs/core_process.md, "Linkage".
 */
#if defined(BGL_SHARED)
#	if defined(_WIN32)
#		ifdef BGL_EXPORTS
#			define BGL_API __declspec(dllexport)
#		else
#			define BGL_API __declspec(dllimport)
#		endif
#	else
#		define BGL_API __attribute__((visibility("default")))
#	endif
#else
#	define BGL_API
#endif
