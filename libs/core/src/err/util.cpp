#include <core/err/util.h>
#include <core/platform/util.h>
#include <cpptrace/basic.hpp>

#include <cpptrace/forward.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <ios>
#include <signal.h>
#include <string>
#include <string_view>

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
			std::ofstream logFile(crash_log_path(), std::ios::out | std::ios::trunc);

			if (!logFile.is_open())
			{
				return;
			}

			logFile << "--- CRASH DETECTED (" << reason << ") ---\n";
			cpptrace::generate_trace().print(logFile);

			// The process is about to be killed, so the stream will not be flushed on its way out.
			logFile.close();
		}

		/**
		 * Names the instruction at `pc`, as `symbol at file:line`.
		 *
		 * The frame no trace below can carry. A trace is walked from the frame pointers, which hold
		 * *return* addresses -- so the innermost function, the one that has not returned, is absent
		 * from every one of them, and that is the function a fault is in. `pc` comes from the state
		 * the fault interrupted, the only place it survives.
		 */
		std::string
		faulting_frame(const void* pc)
		{
			const cpptrace::frame_ptr address = reinterpret_cast<cpptrace::frame_ptr>(pc);

			// Used as-is. The -1 that turns a return address into the call site is applied by
			// cpptrace's unwinder, which a hand-built trace never goes through -- and `pc` is already
			// an instruction pointer, so there is nothing to step back off.
			const cpptrace::stacktrace trace = cpptrace::raw_trace{ { address } }.resolve();

			return trace.frames.empty() ? std::format("0x{:016x}", address) :
			                              trace.frames.front().to_string();
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
			if (info == nullptr)
			{
				write_crash_log("structured exception");
				std::_Exit(3);
			}

			const EXCEPTION_RECORD& record = *info->ExceptionRecord;

			// ExceptionInformation[1] is the address that was accessed, and it is defined for these
			// two codes alone; for any other it holds whatever that exception happens to carry.
			const bool addressed = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
			                       record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR;

			write_crash_log(
				addressed ? std::format(
								"structured exception 0x{:08X}, faulting address 0x{:016x}, {}",
								record.ExceptionCode,
								record.ExceptionInformation[1],
								faulting_frame(record.ExceptionAddress)) :
							std::format(
								"structured exception 0x{:08X}, {}",
								record.ExceptionCode,
								faulting_frame(record.ExceptionAddress)));

			// Not EXECUTE_HANDLER: the default handler it would run can end just the faulting
			// thread, leaving a zombie process whose UI is up but whose worker is gone -- observed
			// as a window that no longer closes. Crash means exit.
			std::_Exit(3);
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

		void
		crash_signal_handle(int signal)
		{
			write_crash_log(std::format("signal {}", signal));
			std::_Exit(signal);
		}
#endif

#if !defined(_WIN32)
		/**
		 * The program counter the signal interrupted, or null where this platform does not carry one.
		 */
		const void*
		interrupted_pc(const void* context)
		{
			const auto* uc = static_cast<const ucontext_t*>(context);
			if (uc == nullptr || uc->uc_mcontext == nullptr)
			{
				return nullptr;
			}

#	if defined(__aarch64__) || defined(__arm64__)
			return reinterpret_cast<const void*>(uc->uc_mcontext->__ss.__pc);
#	elif defined(__x86_64__)
			return reinterpret_cast<const void*>(uc->uc_mcontext->__ss.__rip);
#	else
			return nullptr;
#	endif
		}

		void
		crash_signal_action(int signal, siginfo_t* info, void* context)
		{
			// si_addr shares a union with the sender's pid, and only a hardware fault fills it in. A
			// raised signal -- SIGABRT out of gassert, which is the common way in here -- would
			// otherwise report those bits as an address.
			const bool addressed = info != nullptr && (signal == SIGSEGV || signal == SIGBUS ||
			                                           signal == SIGILL || signal == SIGFPE);

			const std::string frame = faulting_frame(interrupted_pc(context));

			write_crash_log(
				addressed ? std::format(
								"signal {}, faulting address 0x{:016x}, {}",
								signal,
								reinterpret_cast<uintptr_t>(info->si_addr),
								frame) :
							std::format("signal {}, {}", signal, frame));

			// Not exit: a process that has already faulted must not be asked to run its static
			// destructors, whose second crash would overwrite this log with a less informative one.
			std::_Exit(signal);
		}
#endif
	}

	void
	install_crash_handlers()
	{
#if defined(_WIN32)
		std::signal(SIGSEGV, crash_signal_handle);
		std::signal(SIGABRT, crash_signal_handle);
		std::signal(SIGFPE, crash_signal_handle);
		std::signal(SIGILL, crash_signal_handle);
#else
		// sigaction rather than signal: only this form is handed the siginfo_t and the interrupted
		// register state, which carry the address that faulted and the instruction that faulted on it.
		struct sigaction action = {};
		action.sa_sigaction     = crash_signal_action;
		action.sa_flags         = SA_SIGINFO;
		sigemptyset(&action.sa_mask);

		for (const int signal : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS })
		{
			sigaction(signal, &action, nullptr);
		}
#endif

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
