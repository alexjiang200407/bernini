#pragma once

/**
 * Marks what crowdlib exports when it is a shared library (CROWD_SHARED, set by the build); nothing
 * when it is linked statically. Built as the renderer is: see docs/core_process.md, "Linkage".
 */
#if defined(CROWD_SHARED)
#	if defined(_WIN32)
#		ifdef CROWD_EXPORTS
#			define CROWD_API __declspec(dllexport)
#		else
#			define CROWD_API __declspec(dllimport)
#		endif
#	else
#		define CROWD_API __attribute__((visibility("default")))
#	endif
#else
#	define CROWD_API
#endif
