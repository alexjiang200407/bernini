#pragma once
#include "device/Device.h"
#include <bgl/IGraphics.h>

namespace bgl
{
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
		GetDevice() const = 0;
	};
}
