#include "util/TestGraphics.h"
#include "util/GpuValidation.h"

#include <bgl/IGraphics.h>
#include <bgpu/GpuContext.h>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace bgl::test
{
	namespace
	{
		// Written and read on the thread the cases run on; Catch2 runs them one at a time.
		bgpu::GpuContextRef  g_Context;
		bgpu::GpuContextDesc g_Desc;

		// Released before the run ends rather than at static destruction, whose order against the
		// logger and Slang is nobody's to rely on.
		class ReleaseAtRunEnd final : public Catch::EventListenerBase
		{
		public:
			explicit ReleaseAtRunEnd(const Catch::IConfig* config) : EventListenerBase(config) {}

			void
			testRunEnded(const Catch::TestRunStats&) override
			{
				ReleaseGpuContext();
			}
		};

		CATCH_REGISTER_LISTENER(ReleaseAtRunEnd)
	}

	bgl::GraphicsRef
	CreateGraphics(const GraphicsSetup& opts)
	{
		// Under --gpu-validation every device is instrumented anyway (main() turns it on for the
		// process), so a case that did not ask is given the desc of one that did. Otherwise the two
		// kinds alternate, each switch builds a new device, and the PSOs the last context shared are
		// patched all over again -- tens of seconds a case.
		auto desc = opts.gpuContext;
		if (GpuValidationEnabled())
		{
			desc.enableDebugLayer         = true;
			desc.enableGPUValidationLayer = true;
		}

		if (g_Context == nullptr || !(g_Desc == desc))
		{
			g_Context = nullptr;
			g_Context = bgpu::CreateGpuContext(desc);
			g_Desc    = desc;
		}
		return bgl::CreateGraphics(g_Context, opts.graphics);
	}

	void
	ReleaseGpuContext() noexcept
	{
		g_Context = nullptr;
	}
}
