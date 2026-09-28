#pragma once

#include <bgl/IGraphics.h>
#include <device_context/DeviceContext.h>

namespace bgl::test
{
	/**
	 * The two option sets a case fills as one: the device's and the renderer's. The split is the
	 * production API's -- an application creates a device context and hands it to the renderer --
	 * and a case that only wants a renderer need not spell it.
	 */
	struct GraphicsSetup
	{
		gpu::DeviceContextDesc context;
		bgl::GraphicsOptions   graphics;
	};

	/**
	 * A renderer on the suite's device context. The suite is one process with one device, as any
	 * application is: the first call creates the context from `opts.context`, later calls with the
	 * same desc share it -- which is how a case holds two renderers at once -- and a different desc
	 * replaces it once no renderer holds the old one.
	 *
	 * @throws std::runtime_error if a renderer still holds a context of another desc.
	 */
	[[nodiscard]] bgl::GraphicsRef
	CreateGraphics(const GraphicsSetup& opts);

	/**
	 * Drops the suite's hold on its context, for a case that creates one of its own. A renderer that
	 * is alive keeps the context it was built on.
	 */
	void
	ReleaseDeviceContext() noexcept;
}
