#pragma once
#include <core/err/util.h>
#include <fmt/base.h>
#include <spdlog/spdlog.h>
#include <utility>

// The renderer's names for core's checks: the g is for graphics, which is what bgl code has always
// called them. Everywhere else calls core's originals in <core/err/util.h>.
namespace bgl
{
	// The alias every renderer source logs through. It lives here rather than in a subsystem's PCH
	// because this header has to compile on its own.
	namespace logger = spdlog;

	template <typename... Args>
	void
	gassert(bool condition, fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::ensure(condition, msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	[[noreturn]] void
	gfatal(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::fatal(msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	[[noreturn]] void
	gunimplemented(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::unimplemented(msg, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void
	gerror(fmt::format_string<Args...> msg, Args&&... args) noexcept
	{
		core::error(msg, std::forward<Args>(args)...);
	}
}
