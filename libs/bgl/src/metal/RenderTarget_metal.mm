#include "RenderTarget_metal.h"

namespace bgl
{
	RenderTarget::RenderTarget(const RenderTargetDesc& desc, id<MTLDevice> device) :
		m_Width(static_cast<uint32_t>(desc.width)), m_Height(static_cast<uint32_t>(desc.height))
	{
		if (desc.headless || desc.wnd == nullptr)
		{
			core::throw_runtime_error("Metal RenderTarget requires a CAMetalLayer (headless not yet supported)");
		}

		m_Layer                 = (__bridge CAMetalLayer*)desc.wnd;
		m_Layer.device          = device;
		m_Layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
		m_Layer.framebufferOnly = YES;
		m_Layer.drawableSize    = CGSizeMake(m_Width, m_Height);
	}

	RenderTarget::~RenderTarget() noexcept = default;

	void
	RenderTarget::Resize(uint32_t width, uint32_t height) noexcept
	{
		m_Width           = width;
		m_Height          = height;
		m_Layer.drawableSize = CGSizeMake(width, height);
	}
}
