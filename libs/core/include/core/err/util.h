#pragma once

#include <exception>
#include <fmt/base.h>
#include <format>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>

// A breakpoint only under a debug build: __debugbreak() in a shipping build raises a breakpoint
// exception that crashes the process whether or not a debugger is attached.
#if defined(_MSC_VER) && !defined(NDEBUG)
#	define CORE_DEBUG_BREAK() __debugbreak()
#else
#	define CORE_DEBUG_BREAK() ((void)0)
#endif

namespace core
{
	/**
	 * Routes every abnormal exit into `<exe>_crash.log`, with a stack trace and -- where there is one
	 * -- a reason.
	 *
	 * - An **uncaught C++ exception** reaches abort() through std::terminate, so SIGABRT does fire --
	 *   but the exception is gone by then, and its message is the one thing a stack trace cannot
	 *   reconstruct. A terminate handler reads it while it still exists.
	 * - An **access violation** is a structured exception. The CRT does not reliably raise SIGSEGV for
	 *   one, so the signal handler never runs; SetUnhandledExceptionFilter is what does.
	 * - A **CRT debug assertion** (a bad iterator, say) opens a modal dialog, and only calls abort()
	 *   if someone clicks Abort. In a GUI app -- or a CI run -- that dialog blocks forever. A report
	 *   hook writes the log and leaves instead, standing aside when a debugger is attached so the
	 *   assertion still breaks where it can be looked at.
	 *
	 * Idempotent. Call it first thing in main(), before anything can fail.
	 */
	void
	install_crash_handlers();

	template <typename... Args>
	[[noreturn]] void
	throw_runtime_error(std::format_string<Args...> msg, Args&&... args)
	{
		throw std::runtime_error(std::vformat(msg.get(), std::make_format_args(args...)));
	}

	// The engine's checks, reporting through the process's log before they stop: an internal
	// invariant a caller cannot cause, which throw_runtime_error above is not for.
	template <typename... Args>
	void
	ensure(bool condition, fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		if (!condition)
		{
			spdlog::error(msg, std::forward<Args>(args)...);
			CORE_DEBUG_BREAK();
			std::terminate();
		}
	}

	template <typename... Args>
	[[noreturn]] void
	fatal(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::critical(msg, std::forward<Args>(args)...);
		CORE_DEBUG_BREAK();
		std::terminate();
	}

	// A path that is declared but not yet built -- a backend mid-port, a feature slice not landed.
	// Same effect as fatal; the distinct name marks intent at the call site (it *will* be built,
	// as opposed to a genuine invariant violation).
	template <typename... Args>
	[[noreturn]] void
	unimplemented(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::critical(msg, std::forward<Args>(args)...);
		CORE_DEBUG_BREAK();
		std::terminate();
	}

	template <typename... Args>
	void
	error(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		spdlog::error(msg, std::forward<Args>(args)...);
		CORE_DEBUG_BREAK();
	}
}
