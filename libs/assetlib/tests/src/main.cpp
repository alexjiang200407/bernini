#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>
#include <core/err/util.h>

int
main(int argc, char* argv[])
{
	Catch::Session session;
	int            returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0)
		return returnCode;

	core::install_crash_handlers();

	return session.run();
}
