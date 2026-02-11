#pragma once

namespace gfx
{
	template <typename... Args>
	[[noreturn]] void
	gassert(bool condition, fmt::format_string<Args...> msg, Args... args)
	{
		if (!condition)
		{
			logger::error(msg, args...);

#if defined(_MSC_VER)
			__debugbreak();
#endif
			std::terminate();
		}
	}

	template <typename... Args>
	[[noreturn]] void
	gfatal(fmt::format_string<Args...> msg, Args&&... args)
	{
		logger::critical(msg, std::forward<Args>(args)...);

#if defined(_MSC_VER)
		__debugbreak();
#endif
		std::terminate();
	}
}
