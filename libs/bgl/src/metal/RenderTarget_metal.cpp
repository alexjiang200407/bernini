#include "RenderTarget_metal.h"

#include "cmd/CommandQueue_metal.h"
#include "device/Device_metal.h"
#include "resource/ResourceManager_metal.h"
#include "util_metal.h"

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
		CommandQueueRef         queue,
		ResourceManagerRef      resourceManager) :
		m_Device(std::move(device)), m_Queue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager)), m_Width(static_cast<uint32_t>(desc.width)),
		m_Height(static_cast<uint32_t>(desc.height))
	{
		if (!desc.headless)
		{
			if (desc.wnd == nullptr)
			{
				core::throw_runtime_error(
					"Metal backend: a windowed render target needs a CAMetalLayer in "
					"RenderTargetDesc::wnd");
			}

			m_Layer = static_cast<CA::MetalLayer*>(desc.wnd);
			m_Layer->setDevice(m_Device->As<Device>()->GetMTLDevice());

			// Must match the ring's colour format: the present path is a blit, and Metal requires
			// both sides of one to agree.
			m_Layer->setPixelFormat(ConvertFormat(c_BackbufferFormat));

			// A drawable is a blit destination here, not only an attachment, which framebufferOnly
			// would forbid.
			m_Layer->setFramebufferOnly(false);
			m_Layer->setDrawableSize(
				CGSize{ static_cast<CGFloat>(m_Width), static_cast<CGFloat>(m_Height) });
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
	RenderTarget::PresentToLayer() noexcept
	{
		if (m_Layer == nullptr)
			return;

		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// Null when the layer has no drawable free -- the display is holding them all. Dropping the
		// frame is the right answer; the ring already holds it and the next present shows it.
		CA::MetalDrawable* drawable = m_Layer->nextDrawable();
		if (drawable == nullptr)
			return;

		auto*               rm   = m_ResourceManager->As<ResourceManager>();
		const TextureHandle src  = m_Backbuffers[m_FrameIndex].texture;
		MTL::Texture*       from = rm->GetTexture(src).GetMTLResource();
		MTL::Texture*       to   = drawable->texture();

		// Encoded on the renderer's queue, so it is ordered after the frame that filled the ring.
		MTL::CommandBuffer* cmd =
			m_Queue->As<CommandQueue>()->GetMTLCommandQueue()->commandBuffer();
		MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
		blit->copyFromTexture(from, to);
		blit->endEncoding();

		cmd->presentDrawable(drawable);
		cmd->commit();
	}

	void
	RenderTarget::PresentAndAdvance() noexcept
	{
		PresentToLayer();

		m_LastPresentedIndex = m_FrameIndex;
		m_FrameIndex         = (m_FrameIndex + 1) % c_SwapchainImageCount;
	}

	void
	RenderTarget::ResizeBackbuffers(uint32_t width, uint32_t height)
	{
		ReleaseAttachments();

		m_Width  = width;
		m_Height = height;
		if (m_Layer != nullptr)
		{
			m_Layer->setDrawableSize(
				CGSize{ static_cast<CGFloat>(m_Width), static_cast<CGFloat>(m_Height) });
		}
		CreateAttachments();

		// The backbuffers those fences described are gone, so nothing is in flight against the ring.
		m_FrameFences.fill(0);
		m_FrameIndex         = 0;
		m_LastPresentedIndex = 0;
	}
}
