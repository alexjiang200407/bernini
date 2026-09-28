#pragma once

/**
 * Marks what bgpu exports when it is a shared library (BGPU_SHARED, set by the
 * build); nothing when it is linked statically. Built as the renderer is: see docs/core_process.md,
 * "Linkage".
 */
#if defined(BGPU_SHARED)
#	if defined(_WIN32)
#		ifdef BGPU_EXPORTS
#			define BGPU_API __declspec(dllexport)
#		else
#			define BGPU_API __declspec(dllimport)
#		endif
#	else
#		define BGPU_API __attribute__((visibility("default")))
#	endif
#else
#	define BGPU_API
#endif
