#pragma once
#include <core/log/bassert.h>
#include <fmt/base.h>
#include <spdlog/spdlog.h>
#include <utility>

// The renderer's names for core's assertions: the g is for graphics, which is what bgl code has
// always called them. Everywhere else calls core::logging's b-prefixed originals.
#define GDEBUG_BREAK()           BDEBUG_BREAK()
#define GWARN_ONCE(fmt_str, ...) BWARN_ONCE(fmt_str, ##__VA_ARGS__)

namespace bgl
{
	// The alias every renderer source logs through. It lives here rather than in a subsystem's PCH
	// because this header has to compile on its own.
	namespace logger = spdlog;

	template <typename... Args>
	void
	gassert(bool condition, fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::logging::bassert(condition, msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	[[noreturn]] void
	gfatal(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::logging::bfatal(msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	[[noreturn]] void
	gunimplemented(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::logging::bunimplemented(msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void
	gerror(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::logging::berror(msg, std::forward<Args>(args)...);
	}
}
