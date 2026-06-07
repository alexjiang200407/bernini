#pragma once
#include <spdlog/sinks/basic_file_sink.h>

namespace bgl
{
	template <typename... Args>
	void
	gassert(bool condition, fmt::format_string<Args...> msg, Args&&... args)
	{
		if (!condition)
		{
			// Pass fmt::forward to preserve types correctly
			logger::error(msg, std::forward<Args>(args)...);
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
