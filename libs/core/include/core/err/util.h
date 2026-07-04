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
}
