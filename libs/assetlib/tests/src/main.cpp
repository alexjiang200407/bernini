#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>
#include <core/err/util.h>
#include <cpptrace/cpptrace.hpp>
#include <csignal>

int
main(int argc, char* argv[])
{
	Catch::Session session;
	int            returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0)
		return returnCode;

	std::signal(SIGSEGV, core::crash_signal_handle);
	std::signal(SIGABRT, core::crash_signal_handle);
	std::signal(SIGFPE, core::crash_signal_handle);

	return session.run();
}
