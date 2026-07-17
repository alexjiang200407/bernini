#include <core/err/util.h>
#include <core/platform/util.h>
#include <cpptrace/cpptrace.hpp>

#include <csignal>
#include <ctime>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <windows.h>

#	include <crtdbg.h>
#endif

namespace core
{
	namespace
	{
		/**
		 * `{exe}_crash_YYYYMMDD_HHMMSS.log`, next to the executable.
		 */
		std::string
		crash_log_path()
		{
			const std::time_t now = std::time(nullptr);

			std::tm local = {};
#if defined(_WIN32)
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif

			char stamp[32] = {};
			std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);

			return "./" + get_executable_name() + "_crash_" + stamp + ".log";
		}

		void
		write_crash_log(std::string_view reason)
		{
			std::ofstream log_file(crash_log_path(), std::ios::out | std::ios::trunc);

			if (!log_file.is_open())
			{
				return;
			}

			log_file << "--- CRASH DETECTED (" << reason << ") ---\n";
			cpptrace::generate_trace().print(log_file);

			// The process is about to be killed, so the stream will not be flushed on its way out.
			log_file.close();
		}

#if defined(_WIN32)
		/**
		 * An uncaught exception. abort() would raise SIGABRT and the signal handler would write a
		 * stack -- but not what was thrown, and that is the only part the stack cannot show.
		 *
		 * Leaves through _Exit rather than abort: abort re-enters through SIGABRT, whose handler would
		 * overwrite this log with a less informative one.
		 */
		void
		terminate_handler()
		{
			std::string reason = "unhandled exception";

			if (std::current_exception())
			{
				try
				{
					std::rethrow_exception(std::current_exception());
				}
				catch (const std::exception& e)
				{
					reason += ": ";
					reason += e.what();
				}
				catch (...)
				{
					reason += ": (not derived from std::exception)";
				}
			}

			write_crash_log(reason);
			std::_Exit(3);
		}

		LONG WINAPI
		unhandled_exception_filter(EXCEPTION_POINTERS* info)
		{
			const DWORD code = info != nullptr ? info->ExceptionRecord->ExceptionCode : DWORD{ 0 };

			write_crash_log(std::format("structured exception 0x{:08X}", code));

			// Not SEARCH_HANDLERS: nothing above us handles these, and CONTINUE would loop.
			return EXCEPTION_EXECUTE_HANDLER;
		}

#	if defined(_DEBUG)
		// No returnValue: this never hands control back to the CRT, so there is nothing to tell it.
		int
		crt_report_hook(int type, char* message, int*)
		{
			if (type != _CRT_ASSERT && type != _CRT_ERROR)
			{
				return FALSE;  // a warning: leave it to the CRT
			}

			if (IsDebuggerPresent())
			{
				return FALSE;
			}

			write_crash_log(
				std::format(
					"CRT {}: {}",
					type == _CRT_ASSERT ? "assertion" : "error",
					message != nullptr ? message : "(no message)"));

			std::_Exit(3);
		}
#	endif
#endif
	}

	void
	crash_signal_handle(int signal)
	{
		write_crash_log(std::format("signal {}", signal));
		std::exit(signal);
	}

	void
	install_crash_handlers()
	{
		std::signal(SIGSEGV, crash_signal_handle);
		std::signal(SIGABRT, crash_signal_handle);
		std::signal(SIGFPE, crash_signal_handle);
		std::signal(SIGILL, crash_signal_handle);

#if defined(_WIN32)
		std::set_terminate(terminate_handler);
		SetUnhandledExceptionFilter(unhandled_exception_filter);

		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#	if defined(_DEBUG)
		_CrtSetReportHook(crt_report_hook);
#	endif
#endif
	}
}
