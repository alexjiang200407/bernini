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

		// Blocks until the context's queue has drained. Its timeline is registered with the resource
		// manager, so a deferred free stays gated until this has run.
		virtual void
		WaitIdle() noexcept = 0;

		virtual core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const noexcept = 0;
	};
}
