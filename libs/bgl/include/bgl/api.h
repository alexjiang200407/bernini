#pragma once

#if defined(_WIN32)
#	ifdef BGL_EXPORTS
#		define BGL_API __declspec(dllexport)
#	else
#		define BGL_API __declspec(dllimport)
#	endif
#else
#	define BGL_API __attribute__((visibility("default")))
#endif
