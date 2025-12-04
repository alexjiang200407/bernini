#pragma once
#include "GfxBase.h"

namespace gfx
{
	class IGraphics : public GfxBase
	{
	public:
		virtual void
		DrawFrame() = 0;

		nvrhi::DeviceHandle
		GetDevice() noexcept
		{
			assert(
				"IGraphics expects derived class to initialize nvrhiDevice" &&
				nvrhiDevice.Get() != nullptr);
			return nvrhiDevice;
		}

	protected:
		nvrhi::DeviceHandle      nvrhiDevice;
		nvrhi::FramebufferHandle nvrhiFramebuffer;
	};

}
