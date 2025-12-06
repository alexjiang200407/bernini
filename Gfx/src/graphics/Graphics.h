#pragma once
#include "GfxBase.h"

namespace gfx
{
	class Camera;

	class IGraphics : public GfxBase
	{
	public:
		virtual void
		DrawFrame(Camera& camera) = 0;

		[[nodiscard]]
		nvrhi::DeviceHandle
		GetDevice() noexcept
		{
			assert(
				"IGraphics expects derived class to initialize nvrhiDevice" &&
				nvrhiDevice.Get() != nullptr);
			return nvrhiDevice;
		}

	protected:
		int                      windowWidth = 0, windowHeight = 0;
		nvrhi::DeviceHandle      nvrhiDevice;
		nvrhi::FramebufferHandle nvrhiFramebuffer;
	};

}
