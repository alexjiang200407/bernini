#include "util/TestGraphics.h"

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
		if (g_Context == nullptr || !(g_Desc == opts.context))
		{
			g_Context = nullptr;
			g_Context = bgpu::CreateGpuContext(opts.context);
			g_Desc    = opts.context;
		}
		return bgl::CreateGraphics(g_Context, opts.graphics);
	}

	void
	ReleaseGpuContext() noexcept
	{
		g_Context = nullptr;
	}
}
