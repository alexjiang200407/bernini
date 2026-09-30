#include <catch2/catch_all.hpp>
#include <catch2/catch_session.hpp>
#define CATCH_CONFIG_RUNNER
#include "util/GpuValidation.h"
#include <bgpu/GpuContext.h>
#include <core/err/util.h>

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
			"Enable D3D12 GPU-based validation for every case. Slow -- the debug layer patches "
			"each pipeline on first use -- so it is for a verification run rather than "
			"day-to-day; a release build runs it several times faster.");
	session.cli(cli);

	int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0)
		return returnCode;

	// Set before the first test runs, so every CreateGraphics sees it.
	bgl::test::SetGpuValidation(gpuValidation);

	// Validation is the process's once any device asks for it, so a case that does not ask was
	// instrumented only when one that did happened to run first. Asked for here, every case is,
	// whatever the order or the filter.
	if (gpuValidation)
	{
		auto desc                     = bgpu::GpuContextDesc();
		desc.enableDebugLayer         = true;
		desc.enableGPUValidationLayer = true;
		(void)bgpu::CreateGpuContext(desc);
	}

	core::install_crash_handlers();

	return session.run();
}
