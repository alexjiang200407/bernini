#pragma once

namespace core
{
	void
	crash_signal_handle(int signal);

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
