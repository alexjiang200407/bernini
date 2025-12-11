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
				"IGraphics expects derived class to initialize m_nvrhiDevice" &&
				m_nvrhiDevice.Get() != nullptr);
			return m_nvrhiDevice;
		}

	protected:
		int                      windowWidth = 0, windowHeight = 0;
		nvrhi::DeviceHandle      m_nvrhiDevice;
		nvrhi::FramebufferHandle m_nvrhiFramebuffer;
		nvrhi::TextureHandle     m_nvrhiDepthBuffer;
		nvrhi::TextureHandle     m_nvrhiBackBuffer;
		nvrhi::FramebufferInfo   m_framebufferInfo;
	};

}
