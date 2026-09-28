#include "util/TestGraphics.h"

#include <bgl/IGraphics.h>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <device_context/DeviceContext.h>

namespace bgl::test
{
	namespace
	{
		// Written and read on the thread the cases run on; Catch2 runs them one at a time.
		gpu::DeviceContextRef  g_Context;
		gpu::DeviceContextDesc g_Desc;

		// Released before the run ends rather than at static destruction, whose order against the
		// logger and Slang is nobody's to rely on.
		class ReleaseAtRunEnd final : public Catch::EventListenerBase
		{
		public:
			using Catch::EventListenerBase::EventListenerBase;

			void
			testRunEnded(const Catch::TestRunStats&) override
			{
				ReleaseDeviceContext();
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
			g_Context = gpu::CreateDeviceContext(opts.context);
			g_Desc    = opts.context;
		}
		return bgl::CreateGraphics(g_Context, opts.graphics);
	}

	void
	ReleaseDeviceContext() noexcept
	{
		g_Context = nullptr;
	}
}
