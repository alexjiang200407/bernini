#pragma once

#ifdef BGL_EXPORTS
#	define BGL_API __declspec(dllexport)
#else
#	define BGL_API __declspec(dllimport)
#endif
