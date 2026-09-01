#pragma once
#include <spdlog/sinks/basic_file_sink.h>

// A breakpoint only under a debug build: __debugbreak() in a shipping build raises a breakpoint
// exception that crashes the process whether or not a debugger is attached.
#if defined(_MSC_VER) && !defined(NDEBUG)
#	define GDEBUG_BREAK() __debugbreak()
#else
#	define GDEBUG_BREAK() ((void)0)
#endif

#define GWARN_ONCE(fmt_str, ...)                  \
	do                                            \
	{                                             \
		static bool already_warned = false;       \
		if (!already_warned)                      \
		{                                         \
			already_warned = true;                \
			logger::warn(fmt_str, ##__VA_ARGS__); \
		}                                         \
	} while (0)

namespace bgl
{
	template <typename... Args>
	void
	gassert(bool condition, fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		if (!condition)
		{
			// Pass fmt::forward to preserve types correctly
			logger::error(msg, std::forward<Args>(args)...);
			GDEBUG_BREAK();
			std::terminate();
		}
	}

	template <typename... Args>
	[[noreturn]] void
	gfatal(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		logger::critical(msg, std::forward<Args>(args)...);
		GDEBUG_BREAK();
		std::terminate();
	}

	// A path that is declared but not yet built -- a backend mid-port, a feature slice not landed.
	// Same effect as gfatal; the distinct name marks intent at the call site (it *will* be built,
	// as opposed to a genuine invariant violation).
	template <typename... Args>
	[[noreturn]] void
	gunimplemented(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		logger::critical(msg, std::forward<Args>(args)...);
		GDEBUG_BREAK();
		std::terminate();
	}

	template <typename... Args>
	void
	gerror(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		logger::error(msg, std::forward<Args>(args)...);
		GDEBUG_BREAK();
	}
}
