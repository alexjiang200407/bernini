#pragma once
#include <exception>
#include <fmt/base.h>
#include <spdlog/spdlog.h>
#include <utility>

// The engine's assertions, beside the logger they report through: a failed check writes its message
// to the process's log before it stops, so the log is where the reason is found.

// A breakpoint only under a debug build: __debugbreak() in a shipping build raises a breakpoint
// exception that crashes the process whether or not a debugger is attached.
#if defined(_MSC_VER) && !defined(NDEBUG)
#	define BDEBUG_BREAK() __debugbreak()
#else
#	define BDEBUG_BREAK() ((void)0)
#endif

#define BWARN_ONCE(fmt_str, ...)                  \
	do                                            \
	{                                             \
		static bool already_warned = false;       \
		if (!already_warned)                      \
		{                                         \
			already_warned = true;                \
			spdlog::warn(fmt_str, ##__VA_ARGS__); \
		}                                         \
	} while (0)

namespace core::logging
{
	template <typename... Args>
	void
	bassert(bool condition, fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		if (!condition)
		{
			spdlog::error(msg, std::forward<Args>(args)...);
			BDEBUG_BREAK();
			std::terminate();
		}
	}

	template <typename... Args>
	[[noreturn]] void
	bfatal(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::critical(msg, std::forward<Args>(args)...);
		BDEBUG_BREAK();
		std::terminate();
	}

	// A path that is declared but not yet built -- a backend mid-port, a feature slice not landed.
	// Same effect as bfatal; the distinct name marks intent at the call site (it *will* be built,
	// as opposed to a genuine invariant violation).
	template <typename... Args>
	[[noreturn]] void
	bunimplemented(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::critical(msg, std::forward<Args>(args)...);
		BDEBUG_BREAK();
		std::terminate();
	}

	template <typename... Args>
	void
	berror(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::error(msg, std::forward<Args>(args)...);
		BDEBUG_BREAK();
	}
}
