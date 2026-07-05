#pragma once

namespace core
{
	void
	crash_signal_handle(int signal);

	template <typename... Args>
	[[noreturn]] void
	throw_runtime_error(std::format_string<Args...> msg, Args&&... args)
	{
		throw std::runtime_error(std::vformat(msg.get(), std::make_format_args(args...)));
	}

	// NOTE: intentionally NOT [[noreturn]] -- it only throws when `cond` is true and
	// otherwise returns normally, so callers may place it before reachable code (e.g. a
	// bounds check ahead of a return).
	template <typename... Args>
	void
	throw_runtime_error_if(bool cond, std::format_string<Args...> msg, Args&&... args)
	{
		if (cond)
		{
			throw std::runtime_error(std::vformat(msg.get(), std::make_format_args(args...)));
		}
	}

}
