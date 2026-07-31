#include "RenderTarget_metal.h"

namespace bgl
{
	namespace
	{
		constexpr Format c_BackbufferFormat = Format::SBGRA8_UNORM;
		constexpr Format c_DepthFormat      = Format::D24S8;
		constexpr Format c_MotionFormat     = Format::RG16_FLOAT;
	}

	RenderTarget::RenderTarget(
		const RenderTargetDesc& desc,
		DeviceRef               device,
		CommandQueueRef,
		ResourceManagerRef resourceManager) :
		m_Device(std::move(device)), m_ResourceManager(std::move(resourceManager)),
		m_Width(static_cast<uint32_t>(desc.width)), m_Height(static_cast<uint32_t>(desc.height))
	{
		if (!desc.headless)
		{
			core::throw_runtime_error(
				"Metal backend: a windowed render target is not implemented yet");
		}

		for (CommandAllocatorRef& allocator : m_FrameAllocators)
		{
			allocator = m_Device->CreateCommandAllocator();
		}

		CreateAttachments();
	}

	RenderTarget::~RenderTarget() noexcept
	{
		// Nothing else can still be reading these: Graphics idles its context before dropping a
		// target, and the frame ring is this object's alone.
		ReleaseAttachments();
	}

	void
	RenderTarget::CreateAttachments()
	{
		for (uint32_t i = 0; i < c_SwapchainImageCount; ++i)
		{
			auto texDesc          = TextureDesc();
			texDesc.width         = m_Width;
			texDesc.height        = m_Height;
			texDesc.format        = c_BackbufferFormat;
			texDesc.usage         = TextureUsageFlag::kRenderTarget;
			texDesc.initialLayout = BarrierLayout::kRenderTarget;
			texDesc.debugName     = std::format("Offscreen Back Buffer: {}", i);
			texDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_Backbuffers[i].texture = m_ResourceManager->CreateTexture(texDesc);

			auto rtvDesc      = RtvDesc();
			rtvDesc.format    = c_BackbufferFormat;
			rtvDesc.debugName = std::format("Offscreen Back Buffer RTV: {}", i);

			m_Backbuffers[i].rtv = m_ResourceManager->CreateRtv(m_Backbuffers[i].texture, rtvDesc);
		}

		auto depthDesc          = TextureDesc();
		depthDesc.width         = m_Width;
		depthDesc.height        = m_Height;
		depthDesc.format        = c_DepthFormat;
		depthDesc.usage         = TextureUsageFlag::kDepthStencil;
		depthDesc.initialLayout = BarrierLayout::kDepthWrite;
		depthDesc.debugName     = "Depth Buffer";
		depthDesc.clearValue.SetDepthStencil(1.0f, 0);

		m_DepthTexture = m_ResourceManager->CreateTexture(depthDesc);

		auto dsvDesc      = DsvDesc();
		dsvDesc.format    = c_DepthFormat;
		dsvDesc.debugName = "Depth Buffer DSV";

		m_DepthDsv = m_ResourceManager->CreateDsv(m_DepthTexture, dsvDesc);

		auto motionDesc   = TextureDesc();
		motionDesc.width  = m_Width;
		motionDesc.height = m_Height;
		motionDesc.format = c_MotionFormat;
		// kSRV as well: the buffer exists to be resampled by a later pass, and Metal bakes the usage
		// into the texture at creation rather than deriving it from how it is bound.
		motionDesc.usage = TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };
		motionDesc.initialLayout = BarrierLayout::kRenderTarget;
		motionDesc.debugName     = "Motion Vectors";
		motionDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));

		m_MotionTexture = m_ResourceManager->CreateTexture(motionDesc);

		auto motionRtvDesc      = RtvDesc();
		motionRtvDesc.format    = c_MotionFormat;
		motionRtvDesc.debugName = "Motion Vectors RTV";

		m_MotionRtv = m_ResourceManager->CreateRtv(m_MotionTexture, motionRtvDesc);
	}

	void
	RenderTarget::ReleaseAttachments() noexcept
	{
		for (Backbuffer& backbuffer : m_Backbuffers)
		{
			if (!backbuffer.rtv.IsNull())
				m_ResourceManager->DestroyRtv(backbuffer.rtv, false);
			if (!backbuffer.texture.IsNull())
				m_ResourceManager->DestroyTexture(backbuffer.texture, false);
			backbuffer = {};
		}

		if (!m_MotionRtv.IsNull())
			m_ResourceManager->DestroyRtv(m_MotionRtv, false);
		if (!m_MotionTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_MotionTexture, false);
		if (!m_DepthDsv.IsNull())
			m_ResourceManager->DestroyDsv(m_DepthDsv, false);
		if (!m_DepthTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_DepthTexture, false);

		m_MotionRtv     = {};
		m_MotionTexture = {};
		m_DepthDsv      = {};
		m_DepthTexture  = {};
	}

	void
	RenderTarget::PresentAndAdvance() noexcept
	{
		// Nothing to present: a headless target's backbuffer is read by a capture or a screenshot,
		// so advancing round-robin is the whole of it.
		m_LastPresentedIndex = m_FrameIndex;
		m_FrameIndex         = (m_FrameIndex + 1) % c_SwapchainImageCount;
	}

	void
	RenderTarget::ResizeBackbuffers(uint32_t width, uint32_t height)
	{
		ReleaseAttachments();

		m_Width  = width;
		m_Height = height;
		CreateAttachments();

		// The backbuffers those fences described are gone, so nothing is in flight against the ring.
		m_FrameFences.fill(0);
		m_FrameIndex         = 0;
		m_LastPresentedIndex = 0;
	}
}
