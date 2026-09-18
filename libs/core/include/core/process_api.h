#pragma once

/**
 * Marks what `core_process` defines: the state that must exist once per process, however many
 * binaries link core. See docs/core_process.md.
 */
#if defined(CORE_PROCESS_SHARED)
#	if defined(_WIN32)
#		ifdef CORE_PROCESS_EXPORTS
#			define CORE_PROCESS_API __declspec(dllexport)
#		else
#			define CORE_PROCESS_API __declspec(dllimport)
#		endif
#	else
#		define CORE_PROCESS_API __attribute__((visibility("default")))
#	endif
#else
#	define CORE_PROCESS_API
#endif
