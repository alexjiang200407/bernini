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

	template <typename... Args>
	void
	gerror(fmt::format_string<Args...> msg, Args&&... args)
	{
		logger::error(msg, std::forward<Args>(args)...);

#if defined(_MSC_VER)
		__debugbreak();
#endif
	}
}
