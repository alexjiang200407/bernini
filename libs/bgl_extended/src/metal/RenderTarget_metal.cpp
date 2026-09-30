#include "RenderTarget_metal.h"
#include "gfx/frame_constants.h"

#include <CoreFoundation/CFCGTypes.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/metal/native_device.h>
#include <bgpu/metal/native_rhi.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <core/err/util.h>
#include <cstdint>
#include <format>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr bgpu::Format c_BackbufferFormat = bgpu::Format::SBGRA8_UNORM;
		// The layer's spelling of the same format; CreateOutputAttachments checks the two agree.
		constexpr MTL::PixelFormat c_BackbufferPixelFormat = MTL::PixelFormatBGRA8Unorm_sRGB;
		constexpr bgpu::Format     c_DepthFormat           = bgpu::Format::D24S8;

		// Linear HDR: the geometry passes write exposed radiance and the tonemap reads it back.
		// Alpha is carried because the blend state writes destination alpha and the capture path
		// reads it, which rules out the packed three-channel float formats.
		constexpr bgpu::Format c_SceneColorFormat = bgpu::Format::RGBA16_FLOAT;

		constexpr bgpu::Format c_OutlineMaskFormat = bgpu::Format::R8_UNORM;
	}

	RenderTarget::RenderTarget(
		const RenderTargetDesc&  desc,
		bgpu::DeviceRef          device,
		bgpu::CommandQueueRef    queue,
		bgpu::ResourceManagerRef resourceManager) :
		m_Device(std::move(device)), m_Queue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager)), m_TaaEnabled(desc.taaEnabled),
		m_TaaAllocated(desc.taaEnabled)
	{
		SetSize(
			static_cast<uint32_t>(desc.width),
			static_cast<uint32_t>(desc.height),
			desc.renderScale);
		SetTaaReconstructionWidth(desc.taaReconstructionWidth);
		SetTaaSharpness(desc.taaSharpness);

		if (!desc.headless)
		{
			if (desc.wnd == nullptr)
			{
				core::throw_runtime_error(
					"Metal backend: a windowed render target needs a CAMetalLayer in "
					"RenderTargetDesc::wnd");
			}

			m_Layer = static_cast<CA::MetalLayer*>(desc.wnd);
			m_Layer->setDevice(bgpu::GetMtlDevice(m_Device->GetGpuContext()));

			// Must match the ring's colour format: the present path is a blit, and Metal requires
			// both sides of one to agree.
			m_Layer->setPixelFormat(c_BackbufferPixelFormat);

			// A drawable is a blit destination here, not only an attachment, which framebufferOnly
			// would forbid.
			m_Layer->setFramebufferOnly(false);
			m_Layer->setDrawableSize(
				CGSize{ static_cast<CGFloat>(GetWidth()), static_cast<CGFloat>(GetHeight()) });
		}

		for (bgpu::CommandAllocatorRef& allocator : m_FrameAllocators)
		{
			allocator = m_Device->CreateCommandAllocator();
		}

		CreateAttachments();
	}

	RenderTarget::~RenderTarget() noexcept
	{
		// Another target's frame may still be sampling this ring through an overlay texture, and
		// the release below is immediate.
		m_Queue->Flush();
		ReleaseAttachments();
	}

	void
	RenderTarget::CreateAttachments()
	{
		CreateOutputAttachments();
		CreateRenderAttachments();
	}

	void
	RenderTarget::CreateOutputAttachments()
	{
		for (uint32_t i = 0; i < c_SwapchainImageCount; ++i)
		{
			auto texDesc          = bgpu::TextureDesc();
			texDesc.width         = GetWidth();
			texDesc.height        = GetHeight();
			texDesc.format        = c_BackbufferFormat;
			texDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                        bgpu::TextureUsageFlag::kSRV };
			texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
			texDesc.debugName     = std::format("Offscreen Back Buffer: {}", i);
			texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_Backbuffers[i].texture = m_ResourceManager->CreateTexture(texDesc);
			core::ensure(
				bgpu::GetMtlTexture(*m_ResourceManager, m_Backbuffers[i].texture)->pixelFormat() ==
					c_BackbufferPixelFormat,
				"the present blit needs the layer and the ring in one format");

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_BackbufferFormat;
			rtvDesc.debugName = std::format("Offscreen Back Buffer RTV: {}", i);

			m_Backbuffers[i].rtv = m_ResourceManager->CreateRtv(m_Backbuffers[i].texture, rtvDesc);

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_BackbufferFormat;
			srvDesc.debugName = std::format("Offscreen Back Buffer SRV: {}", i);

			m_Backbuffers[i].srv = m_ResourceManager->CreateSrv(m_Backbuffers[i].texture, srvDesc);
		}

		if (!m_TaaAllocated)
		{
			return;
		}

		for (uint32_t i = 0; i < m_History.size(); ++i)
		{
			auto historyDesc          = bgpu::TextureDesc();
			historyDesc.width         = GetWidth();
			historyDesc.height        = GetHeight();
			historyDesc.format        = c_SceneColorFormat;
			historyDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                            bgpu::TextureUsageFlag::kSRV };
			historyDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
			historyDesc.debugName     = std::format("TAA History: {}", i);
			historyDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_History[i].texture = m_ResourceManager->CreateTexture(historyDesc);

			auto historyRtvDesc      = bgpu::RtvDesc();
			historyRtvDesc.format    = c_SceneColorFormat;
			historyRtvDesc.debugName = std::format("TAA History RTV: {}", i);

			m_History[i].rtv = m_ResourceManager->CreateRtv(m_History[i].texture, historyRtvDesc);

			auto historySrvDesc      = bgpu::SrvDesc();
			historySrvDesc.format    = c_SceneColorFormat;
			historySrvDesc.debugName = std::format("TAA History SRV: {}", i);

			m_History[i].srv = m_ResourceManager->CreateSrv(m_History[i].texture, historySrvDesc);
		}
	}

	void
	RenderTarget::CreateRenderAttachments()
	{
		auto depthDesc          = bgpu::TextureDesc();
		depthDesc.width         = GetRenderWidth();
		depthDesc.height        = GetRenderHeight();
		depthDesc.format        = c_DepthFormat;
		depthDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kDepthStencil,
			                                          bgpu::TextureUsageFlag::kSRV };
		depthDesc.initialLayout = bgpu::BarrierLayout::kDepthWrite;
		depthDesc.debugName     = "Depth Buffer";
		depthDesc.clearValue.SetDepthStencil(1.0f, 0);

		m_DepthTexture = m_ResourceManager->CreateTexture(depthDesc);

		auto dsvDesc      = bgpu::DsvDesc();
		dsvDesc.format    = c_DepthFormat;
		dsvDesc.debugName = "Depth Buffer DSV";

		m_DepthDsv = m_ResourceManager->CreateDsv(m_DepthTexture, dsvDesc);

		auto depthSrvDesc      = bgpu::SrvDesc();
		depthSrvDesc.format    = c_DepthFormat;
		depthSrvDesc.debugName = "Depth Buffer SRV";

		m_DepthSrv = m_ResourceManager->CreateSrv(m_DepthTexture, depthSrvDesc);

		auto motionDesc   = bgpu::TextureDesc();
		motionDesc.width  = GetRenderWidth();
		motionDesc.height = GetRenderHeight();
		motionDesc.format = c_MotionVectorFormat;
		// kSRV as well: the buffer exists to be resampled by a later pass, and Metal bakes the usage
		// into the texture at creation rather than deriving it from how it is bound.
		motionDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
			                                           bgpu::TextureUsageFlag::kSRV };
		motionDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
		motionDesc.debugName     = "Motion Vectors";
		motionDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 0.0f));

		m_MotionTexture = m_ResourceManager->CreateTexture(motionDesc);

		auto motionRtvDesc      = bgpu::RtvDesc();
		motionRtvDesc.format    = c_MotionVectorFormat;
		motionRtvDesc.debugName = "Motion Vectors RTV";

		m_MotionRtv = m_ResourceManager->CreateRtv(m_MotionTexture, motionRtvDesc);

		auto sceneColorDesc          = bgpu::TextureDesc();
		sceneColorDesc.width         = GetRenderWidth();
		sceneColorDesc.height        = GetRenderHeight();
		sceneColorDesc.format        = c_SceneColorFormat;
		sceneColorDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
			                                               bgpu::TextureUsageFlag::kSRV };
		sceneColorDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
		sceneColorDesc.debugName     = "Scene Color";
		sceneColorDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

		m_SceneColorTexture = m_ResourceManager->CreateTexture(sceneColorDesc);

		auto sceneColorRtvDesc      = bgpu::RtvDesc();
		sceneColorRtvDesc.format    = c_SceneColorFormat;
		sceneColorRtvDesc.debugName = "Scene Color RTV";

		m_SceneColorRtv = m_ResourceManager->CreateRtv(m_SceneColorTexture, sceneColorRtvDesc);

		auto sceneColorSrvDesc      = bgpu::SrvDesc();
		sceneColorSrvDesc.format    = c_SceneColorFormat;
		sceneColorSrvDesc.debugName = "Scene Color SRV";

		m_SceneColorSrv = m_ResourceManager->CreateSrv(m_SceneColorTexture, sceneColorSrvDesc);

		auto motionSrvDesc      = bgpu::SrvDesc();
		motionSrvDesc.format    = c_MotionVectorFormat;
		motionSrvDesc.debugName = "Motion Vectors SRV";

		m_MotionSrv = m_ResourceManager->CreateSrv(m_MotionTexture, motionSrvDesc);

		auto maskDesc          = bgpu::TextureDesc();
		maskDesc.width         = GetRenderWidth();
		maskDesc.height        = GetRenderHeight();
		maskDesc.format        = c_OutlineMaskFormat;
		maskDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
			                                         bgpu::TextureUsageFlag::kSRV };
		maskDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
		maskDesc.debugName     = "Outline Mask";
		maskDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 0.0f));

		m_OutlineMaskTexture = m_ResourceManager->CreateTexture(maskDesc);

		auto maskRtvDesc      = bgpu::RtvDesc();
		maskRtvDesc.format    = c_OutlineMaskFormat;
		maskRtvDesc.debugName = "Outline Mask RTV";

		m_OutlineMaskRtv = m_ResourceManager->CreateRtv(m_OutlineMaskTexture, maskRtvDesc);

		auto maskSrvDesc      = bgpu::SrvDesc();
		maskSrvDesc.format    = c_OutlineMaskFormat;
		maskSrvDesc.debugName = "Outline Mask SRV";

		m_OutlineMaskSrv = m_ResourceManager->CreateSrv(m_OutlineMaskTexture, maskSrvDesc);
	}

	void
	RenderTarget::ReleaseAttachments() noexcept
	{
		ReleaseOutputAttachments();
		ReleaseRenderAttachments();
	}

	void
	RenderTarget::ReleaseOutputAttachments() noexcept
	{
		for (Backbuffer& backbuffer : m_Backbuffers)
		{
			if (!backbuffer.srv.IsNull())
				m_ResourceManager->DestroySrv(backbuffer.srv, false);
			if (!backbuffer.rtv.IsNull())
				m_ResourceManager->DestroyRtv(backbuffer.rtv, false);
			if (!backbuffer.texture.IsNull())
				m_ResourceManager->DestroyTexture(backbuffer.texture, false);
			backbuffer = {};
		}

		for (Accumulation& history : m_History)
		{
			if (!history.srv.IsNull())
				m_ResourceManager->DestroySrv(history.srv, false);
			if (!history.rtv.IsNull())
				m_ResourceManager->DestroyRtv(history.rtv, false);
			if (!history.texture.IsNull())
				m_ResourceManager->DestroyTexture(history.texture, false);
			history = {};
		}

		m_HistoryValid        = false;
		m_CurrentHistoryIndex = 0;
	}

	void
	RenderTarget::ReleaseRenderAttachments() noexcept
	{
		// The accumulation describes samples the new grid does not take, so whatever rebuilds these
		// starts it over -- the buffers themselves are the output's and stay.
		m_HistoryValid = false;

		if (!m_OutlineMaskSrv.IsNull())
			m_ResourceManager->DestroySrv(m_OutlineMaskSrv, false);
		if (!m_OutlineMaskRtv.IsNull())
			m_ResourceManager->DestroyRtv(m_OutlineMaskRtv, false);
		if (!m_OutlineMaskTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_OutlineMaskTexture, false);
		if (!m_DepthSrv.IsNull())
			m_ResourceManager->DestroySrv(m_DepthSrv, false);
		if (!m_MotionSrv.IsNull())
			m_ResourceManager->DestroySrv(m_MotionSrv, false);
		if (!m_SceneColorSrv.IsNull())
			m_ResourceManager->DestroySrv(m_SceneColorSrv, false);
		if (!m_SceneColorRtv.IsNull())
			m_ResourceManager->DestroyRtv(m_SceneColorRtv, false);
		if (!m_SceneColorTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_SceneColorTexture, false);
		if (!m_MotionRtv.IsNull())
			m_ResourceManager->DestroyRtv(m_MotionRtv, false);
		if (!m_MotionTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_MotionTexture, false);
		if (!m_DepthDsv.IsNull())
			m_ResourceManager->DestroyDsv(m_DepthDsv, false);
		if (!m_DepthTexture.IsNull())
			m_ResourceManager->DestroyTexture(m_DepthTexture, false);

		m_OutlineMaskSrv     = {};
		m_OutlineMaskRtv     = {};
		m_OutlineMaskTexture = {};

		m_DepthSrv          = {};
		m_MotionSrv         = {};
		m_SceneColorSrv     = {};
		m_SceneColorRtv     = {};
		m_SceneColorTexture = {};
		m_MotionRtv         = {};
		m_MotionTexture     = {};
		m_DepthDsv          = {};
		m_DepthTexture      = {};
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

		const bgpu::TextureHandle src  = m_Backbuffers[m_FrameIndex].texture;
		MTL::Texture*             from = bgpu::GetMtlTexture(*m_ResourceManager, src);
		MTL::Texture*             to   = drawable->texture();

		// Encoded on the renderer's queue, so it is ordered after the frame that filled the ring.
		MTL::CommandBuffer*      cmd  = bgpu::NewMtlCommandBuffer(*m_Queue);
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
		m_Presented          = true;
	}

	void
	RenderTarget::ResizeBackbuffers(uint32_t width, uint32_t height)
	{
		ReleaseAttachments();

		SetSize(width, height, GetRenderScale());

		if (m_Layer != nullptr)
		{
			m_Layer->setDrawableSize(
				CGSize{ static_cast<CGFloat>(GetWidth()), static_cast<CGFloat>(GetHeight()) });
		}
		CreateAttachments();

		// The backbuffers those fences described are gone, so nothing is in flight against the ring.
		m_FrameFences.fill(0);
		m_FrameIndex         = 0;
		m_LastPresentedIndex = 0;
		m_Presented          = false;
	}

	void
	RenderTarget::SetRenderScale(float scale)
	{
		if (scale == GetRenderScale())
		{
			return;
		}

		// Only what the render size sizes. The backbuffers and the histories are the output's and a
		// scale does not move it, so the frame ring keeps describing textures that still exist and
		// needs no reset.
		ReleaseRenderAttachments();
		SetSize(GetWidth(), GetHeight(), scale);
		CreateRenderAttachments();
	}

	RenderTargetRef
	CreateBackendRenderTarget(
		const RenderTargetDesc&  desc,
		bgpu::DeviceRef          device,
		bgpu::CommandQueueRef    queue,
		bgpu::ResourceManagerRef resourceManager,
		bool)
	{
		return core::SharedRef<RenderTarget>::Make(
			desc,
			std::move(device),
			std::move(queue),
			std::move(resourceManager));
	}
}
