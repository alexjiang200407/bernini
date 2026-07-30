#define CATCH_CONFIG_RUNNER
#include "util/GpuValidation.h"
#include <catch2/catch_all.hpp>
#include <core/err/util.h>
#include <cpptrace/cpptrace.hpp>
#include <csignal>

int
main(int argc, char* argv[])
{
	Catch::Session session;

	// Opt-in: GPU-based validation is very nearly what this suite's runtime is made of. See
	// bgl::test::GpuValidationEnabled. The D3D12 debug layer is a separate thing and stays on.
	bool       gpuValidation = false;
	const auto cli =
		session.cli() |
		Catch::Clara::Opt(gpuValidation)["--gpu-validation"](
			"Enable D3D12 GPU-based validation. Slow -- it patches every shader, taking device "
			"creation from ~3s to ~18s and roughly doubling the suite -- so it is for a final "
			"verification run rather than day-to-day.");
	session.cli(cli);

	int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0)
		return returnCode;

#if defined(RENDERER_BACKEND_METAL)
	// The Metal backend implements only part of the RHI so far; with no filter on the command line,
	// default to the tests tagged [metal] (those known to run on it) so a bare `just test` stays
	// green. Any explicit filter overrides this.
	if (session.configData().testsOrTags.empty())
		session.configData().testsOrTags.emplace_back("[metal]");
#endif

	// Set before the first test runs, so every CreateGraphics sees it.
	bgl::test::SetGpuValidation(gpuValidation);

	std::signal(SIGSEGV, core::crash_signal_handle);
	std::signal(SIGABRT, core::crash_signal_handle);
	std::signal(SIGFPE, core::crash_signal_handle);

	return session.run();
}
