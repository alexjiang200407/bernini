#pragma once
#include "device/Device.h"
#include <bgl/Graphics.h>

namespace bgl
{
	class GraphicsBase : public IGraphics
	{
	public:
		virtual IDevice*
		GetDevice() const = 0;
	};
}
