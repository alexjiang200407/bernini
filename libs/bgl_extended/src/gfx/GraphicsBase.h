#pragma once
#include <bgl/IGraphics.h>
#include <core/ref/SharedRef.h>

namespace bgpu
{}

namespace bgpu
{
	class IDevice;
	class IResourceManager;
}

namespace bgl
{
	class RenderContext;

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

		virtual bgpu::IDevice*
		GetDevice() const noexcept = 0;

		virtual core::SharedRef<bgpu::IResourceManager>
		GetResourceManagerCpy() const noexcept = 0;

		virtual const RenderContext*
		GetRenderContext() const noexcept = 0;
	};
}
