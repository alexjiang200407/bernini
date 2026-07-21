#pragma once
#include <bgl/IGraphics.h>

namespace bgl
{
	class IDevice;
	class IResourceManager;

	class GraphicsBase : public IGraphics
	{
	public:
		GraphicsBase()                             = default;
		GraphicsBase(const GraphicsBase&) noexcept = delete;
		GraphicsBase(GraphicsBase&&) noexcept      = delete;

		GraphicsBase&
		operator=(const GraphicsBase&) noexcept = delete;

		GraphicsBase&
		operator=(GraphicsBase&&) noexcept = delete;

		virtual IDevice*
		GetDevice() const noexcept = 0;

		virtual core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const noexcept = 0;

		// Blocks until the GPU has finished all submitted work. Internal/test facility -- lets a
		// deferred-destroy gate be reached without driving frames.
		virtual void
		WaitIdle() noexcept = 0;
	};
}
