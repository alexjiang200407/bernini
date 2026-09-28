#pragma once

/**
 * Marks what device_context exports when it is a shared library (DEVICE_CONTEXT_SHARED, set by the
 * build); nothing when it is linked statically. Built as the renderer is: see docs/core_process.md,
 * "Linkage".
 */
#if defined(DEVICE_CONTEXT_SHARED)
#	if defined(_WIN32)
#		ifdef DEVICE_CONTEXT_EXPORTS
#			define DEVICE_CONTEXT_API __declspec(dllexport)
#		else
#			define DEVICE_CONTEXT_API __declspec(dllimport)
#		endif
#	else
#		define DEVICE_CONTEXT_API __attribute__((visibility("default")))
#	endif
#else
#	define DEVICE_CONTEXT_API
#endif
