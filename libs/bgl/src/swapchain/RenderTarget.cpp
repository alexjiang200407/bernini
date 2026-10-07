#include "swapchain/RenderTarget.h"
#include "gfx/frame_constants.h"
#include "swapchain/Swapchain.h"
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <cstdint>
#include <format>
#include <spdlog/spdlog.h>
#include <utility>

namespace bgl
{
	RenderTarget::RenderTarget(
		const RenderTargetDesc&    desc,
		std::unique_ptr<Swapchain> swapchain,
		bgpu::DeviceRef            device,
		bgpu::CommandQueueRef      queue,
		bgpu::ResourceManagerRef   resourceManager) :
		m_Device(std::move(device)), m_CommandQueue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager)), m_Headless(desc.headless),
		m_TaaEnabled(desc.taaEnabled), m_TaaAllocated(desc.taaEnabled),
		m_Swapchain(std::move(swapchain))
	{
		SetSize(
			static_cast<uint32_t>(desc.width),
			static_cast<uint32_t>(desc.height),
			desc.renderScale);
		SetTaaReconstructionWidth(desc.taaReconstructionWidth);
		SetTaaSharpness(desc.taaSharpness);
		SetPostProcess(desc.postProcess);

		for (uint32_t i = 0; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i] = m_Device->CreateCommandAllocator();
		}

		core::ensure(m_Headless == (m_Swapchain == nullptr), "A windowed target needs a swapchain");
		if (!m_Headless)
		{
			m_SlotImage[0] = m_Swapchain->GetCurrentImage();
			CreateRenderTargets();
		}
		else
		{
			CreateOffscreenRenderTargets();
		}
	}

	RenderTarget::~RenderTarget() noexcept
	{
		spdlog::trace("~RenderTarget");

		// Idle the GPU so no in-flight frame still references the backbuffers we free.
		m_CommandQueue->Flush();

		DestroyRenderTargets();

		m_Swapchain.reset();

		for (uint32_t i = 0; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i].Reset();
		}
	}

	void
	RenderTarget::CreateRenderTargets()
	{
		CreateBackbuffers();
		CreateAttachments();
	}

	void
	RenderTarget::CreateBackbuffers()
	{
		const std::vector<bgpu::NativeTextureDesc> images = m_Swapchain->GetImages();
		m_BackBuffers.assign(images.size(), {});
		m_ImageDrawn.assign(images.size(), false);

		for (uint32_t i = 0; i < images.size(); i++)
		{
			m_BackBuffers[i].textureHandle = m_ResourceManager->ImportNativeTexture(images[i]);

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = m_Swapchain->GetViewFormat();
			rtvDesc.debugName = std::format("Back Buffer RTV: {}", i);

			m_BackBuffers[i].rtvHandle =
				m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
		}
	}

	void
	RenderTarget::CreateOffscreenRenderTargets()
	{
		{
			m_BackBuffers.assign(c_SwapchainImageCount, {});
			for (uint32_t i = 0; i < c_SwapchainImageCount; i++)
			{
				m_SlotImage[i] = i;

				auto texDesc      = bgpu::TextureDesc();
				texDesc.width     = GetWidth();
				texDesc.height    = GetHeight();
				texDesc.debugName = std::format("Offscreen Back Buffer: {}", i);
				texDesc.dimension = bgpu::TextureDimension::kTexture2D;
				texDesc.format    = bgpu::Format::SBGRA8_UNORM;
				texDesc.usage     = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
					                                    bgpu::TextureUsageFlag::kSRV };
				texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

				m_BackBuffers[i].textureHandle = m_ResourceManager->CreateTexture(texDesc);

				auto rtvDesc      = bgpu::RtvDesc();
				rtvDesc.format    = bgpu::Format::SBGRA8_UNORM;
				rtvDesc.debugName = std::format("Offscreen Back Buffer RTV: {}", i);

				m_BackBuffers[i].rtvHandle =
					m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);

				auto srvDesc      = bgpu::SrvDesc();
				srvDesc.format    = bgpu::Format::SBGRA8_UNORM;
				srvDesc.debugName = std::format("Offscreen Back Buffer SRV: {}", i);

				m_BackBuffers[i].srvHandle =
					m_ResourceManager->CreateSrv(m_BackBuffers[i].textureHandle, srvDesc);
			}
		}

		CreateAttachments();
	}

	namespace
	{
		// Linear HDR: the geometry passes write exposed radiance and the tonemap reads it back.
		// Alpha is carried because the blend state writes destination alpha and the capture path
		// reads it, which rules out the packed three-channel float formats.
		constexpr auto c_SceneColorFormat = bgpu::Format::RGBA16_FLOAT;

		constexpr auto c_OutlineMaskFormat = bgpu::Format::R8_UNORM;
	}

	void
	RenderTarget::CreateAttachments()
	{
		CreateRenderAttachments();
		CreateHistoryAttachments();
	}

	void
	RenderTarget::CreateRenderAttachments()
	{
		{
			auto depthTextureDesc      = bgpu::TextureDesc();
			depthTextureDesc.format    = bgpu::Format::D24S8;
			depthTextureDesc.width     = GetRenderWidth();
			depthTextureDesc.height    = GetRenderHeight();
			depthTextureDesc.dimension = bgpu::TextureDimension::kTexture2D;
			depthTextureDesc.debugName = "Depth Buffer";
			depthTextureDesc.usage     = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kDepthStencil,
				                                             bgpu::TextureUsageFlag::kSRV };
			depthTextureDesc.initialLayout = bgpu::BarrierLayout::kDepthWrite;

			depthTextureDesc.clearValue.SetDepthStencil(1.0f, 0);

			m_DepthBuffer.textureHandle = m_ResourceManager->CreateTexture(depthTextureDesc);

			auto dsvDesc      = bgpu::DsvDesc();
			dsvDesc.format    = bgpu::Format::D24S8;
			dsvDesc.debugName = "Depth Buffer RTV";

			m_DepthBuffer.dsvHandle =
				m_ResourceManager->CreateDsv(m_DepthBuffer.textureHandle, dsvDesc);

			auto depthSrvDesc      = bgpu::SrvDesc();
			depthSrvDesc.format    = bgpu::Format::D24S8;
			depthSrvDesc.debugName = "Depth Buffer SRV";

			m_DepthBuffer.srvHandle =
				m_ResourceManager->CreateSrv(m_DepthBuffer.textureHandle, depthSrvDesc);
		}

		{
			// kSRV as well as kRenderTarget: the buffer exists to be resampled by a later pass.
			auto motionTextureDesc      = bgpu::TextureDesc();
			motionTextureDesc.format    = c_MotionVectorFormat;
			motionTextureDesc.width     = GetRenderWidth();
			motionTextureDesc.height    = GetRenderHeight();
			motionTextureDesc.dimension = bgpu::TextureDimension::kTexture2D;
			motionTextureDesc.debugName = "Motion Vectors";
			motionTextureDesc.usage     = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                              bgpu::TextureUsageFlag::kSRV };
			motionTextureDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;

			motionTextureDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 0.0f));

			m_MotionVectors.textureHandle = m_ResourceManager->CreateTexture(motionTextureDesc);

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_MotionVectorFormat;
			rtvDesc.debugName = "Motion Vectors RTV";

			m_MotionVectors.rtvHandle =
				m_ResourceManager->CreateRtv(m_MotionVectors.textureHandle, rtvDesc);
		}

		{
			auto sceneColorDesc      = bgpu::TextureDesc();
			sceneColorDesc.format    = c_SceneColorFormat;
			sceneColorDesc.width     = GetRenderWidth();
			sceneColorDesc.height    = GetRenderHeight();
			sceneColorDesc.dimension = bgpu::TextureDimension::kTexture2D;
			sceneColorDesc.debugName = "Scene Color";
			sceneColorDesc.usage     = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                           bgpu::TextureUsageFlag::kSRV };
			sceneColorDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;

			sceneColorDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_SceneColor.textureHandle = m_ResourceManager->CreateTexture(sceneColorDesc);

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_SceneColorFormat;
			rtvDesc.debugName = "Scene Color RTV";

			m_SceneColor.rtvHandle =
				m_ResourceManager->CreateRtv(m_SceneColor.textureHandle, rtvDesc);

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_SceneColorFormat;
			srvDesc.debugName = "Scene Color SRV";

			m_SceneColor.srvHandle =
				m_ResourceManager->CreateSrv(m_SceneColor.textureHandle, srvDesc);
		}

		{
			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_MotionVectorFormat;
			srvDesc.debugName = "Motion Vectors SRV";

			m_MotionVectorSrv =
				m_ResourceManager->CreateSrv(m_MotionVectors.textureHandle, srvDesc);
		}

		{
			auto maskDesc          = bgpu::TextureDesc();
			maskDesc.format        = c_OutlineMaskFormat;
			maskDesc.width         = GetRenderWidth();
			maskDesc.height        = GetRenderHeight();
			maskDesc.dimension     = bgpu::TextureDimension::kTexture2D;
			maskDesc.debugName     = "Outline Mask";
			maskDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                         bgpu::TextureUsageFlag::kSRV };
			maskDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;

			maskDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 0.0f));

			m_OutlineMask.textureHandle = m_ResourceManager->CreateTexture(maskDesc);

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_OutlineMaskFormat;
			rtvDesc.debugName = "Outline Mask RTV";

			m_OutlineMask.rtvHandle =
				m_ResourceManager->CreateRtv(m_OutlineMask.textureHandle, rtvDesc);

			auto srvDesc      = bgpu::SrvDesc();
			srvDesc.format    = c_OutlineMaskFormat;
			srvDesc.debugName = "Outline Mask SRV";

			m_OutlineMask.srvHandle =
				m_ResourceManager->CreateSrv(m_OutlineMask.textureHandle, srvDesc);
		}
	}

	void
	RenderTarget::CreateHistoryAttachments()
	{
		if (!m_TaaAllocated)
		{
			return;
		}

		for (uint32_t i = 0; i < m_History.size(); ++i)
		{
			auto historyDesc          = bgpu::TextureDesc();
			historyDesc.format        = c_SceneColorFormat;
			historyDesc.width         = GetWidth();
			historyDesc.height        = GetHeight();
			historyDesc.dimension     = bgpu::TextureDimension::kTexture2D;
			historyDesc.debugName     = std::format("TAA History: {}", i);
			historyDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
				                                            bgpu::TextureUsageFlag::kSRV };
			historyDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
			historyDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_History[i].textureHandle = m_ResourceManager->CreateTexture(historyDesc);

			auto rtvDesc      = bgpu::RtvDesc();
			rtvDesc.format    = c_SceneColorFormat;
			rtvDesc.debugName = std::format("TAA History RTV: {}", i);

			m_History[i].rtvHandle =
				m_ResourceManager->CreateRtv(m_History[i].textureHandle, rtvDesc);

			auto historySrvDesc      = bgpu::SrvDesc();
			historySrvDesc.format    = c_SceneColorFormat;
			historySrvDesc.debugName = std::format("TAA History SRV: {}", i);

			m_History[i].srvHandle =
				m_ResourceManager->CreateSrv(m_History[i].textureHandle, historySrvDesc);
		}
	}

	void
	RenderTarget::PresentAndAdvance() noexcept
	{
		const uint32_t slot = m_FrameIndex;

		if (m_Swapchain)
		{
			m_ImageDrawn[m_SlotImage[slot]] = true;
			if (m_Swapchain->Present(m_FenceValues[slot]))
			{
				ReimportBackbuffers();
				return;
			}
		}

		// Recorded before advancing: a readback samples the frame that was just presented, not the
		// one about to be recorded.
		m_LastPresentedIndex = slot;
		m_Presented          = true;

		m_FrameIndex = (slot + 1) % c_SwapchainImageCount;
		if (m_Swapchain)
		{
			m_SlotImage[m_FrameIndex] = m_Swapchain->GetCurrentImage();
		}
	}

	void
	RenderTarget::ReimportBackbuffers()
	{
		DestroyBackbuffers();
		CreateBackbuffers();
		ResetFrameRing();
	}

	bgpu::BarrierLayout
	RenderTarget::GetBackbufferLayout(const uint32_t frameIndex) const noexcept
	{
		core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
		if (m_Swapchain && m_Swapchain->StartsUndefined() && !m_ImageDrawn[m_SlotImage[frameIndex]])
		{
			return bgpu::BarrierLayout::kUndefined;
		}
		return bgpu::BarrierLayout::kPresent;
	}

	void
	RenderTarget::ResizeBackbuffers(uint32_t width, uint32_t height)
	{
		DestroyRenderTargets();

		SetSize(width, height, GetRenderScale());

		if (m_Swapchain)
		{
			m_Swapchain->Resize(width, height);
		}

		RecreateRenderTargets();
	}

	void
	RenderTarget::SetRenderScale(float scale)
	{
		if (scale == GetRenderScale())
		{
			return;
		}

		// Only what the render size sizes. The swapchain, its backbuffers and the histories are all
		// the output's and a scale does not move it, so the frame ring keeps describing textures
		// that still exist and needs no reset.
		DestroyRenderAttachments();
		SetSize(GetWidth(), GetHeight(), scale);
		CreateRenderAttachments();
	}

	void
	RenderTarget::RecreateRenderTargets()
	{
		if (m_Swapchain)
		{
			CreateRenderTargets();
		}
		else
		{
			CreateOffscreenRenderTargets();
		}
		ResetFrameRing();
	}

	void
	RenderTarget::ResetFrameRing() noexcept
	{
		m_FrameIndex = 0;
		if (m_Swapchain)
		{
			m_SlotImage[0] = m_Swapchain->GetCurrentImage();
		}

		// The backbuffers these fences described no longer exist, so the next frame must not wait
		// on them.
		for (auto& slotFence : m_FenceValues)
		{
			slotFence = 0;
		}

		m_LastPresentedIndex = m_FrameIndex;
		m_Presented          = false;
	}

	void
	RenderTarget::DestroyRenderTargets()
	{
		DestroyBackbuffers();
		DestroyHistoryAttachments();
		DestroyRenderAttachments();
	}

	void
	RenderTarget::DestroyBackbuffers() noexcept
	{
		// Every handle is checked before it is released, as the Metal backend's counterpart does.
		// A view is null when it was never made -- a windowed target's swapchain images get no
		// SRV -- and also when its pool was exhausted, because CreateRtv reports that by returning
		// a null handle rather than throwing. Cleared afterwards so a second teardown is a no-op.
		for (const TextureRtvSrvHandle& backBuffer : m_BackBuffers)
		{
			if (!backBuffer.srvHandle.IsNull())
			{
				m_ResourceManager->DestroySrv(backBuffer.srvHandle, false);
			}
			if (!backBuffer.rtvHandle.IsNull())
			{
				m_ResourceManager->DestroyRtv(backBuffer.rtvHandle, false);
			}
			if (!backBuffer.textureHandle.IsNull())
			{
				m_ResourceManager->DestroyTexture(backBuffer.textureHandle, false);
			}
		}
		m_BackBuffers.clear();
	}

	void
	RenderTarget::DestroyHistoryAttachments()
	{
		for (TextureRtvSrvHandle& history : m_History)
		{
			if (!history.srvHandle.IsNull())
			{
				m_ResourceManager->DestroySrv(history.srvHandle, false);
			}
			if (!history.rtvHandle.IsNull())
			{
				m_ResourceManager->DestroyRtv(history.rtvHandle, false);
			}
			if (!history.textureHandle.IsNull())
			{
				m_ResourceManager->DestroyTexture(history.textureHandle, false);
			}

			history = {};
		}

		m_HistoryValid        = false;
		m_CurrentHistoryIndex = 0;
	}

	void
	RenderTarget::DestroyRenderAttachments()
	{
		// The accumulation describes samples the new grid does not take, so whatever rebuilds these
		// starts it over -- the buffers themselves are the output's and stay.
		m_HistoryValid = false;

		if (!m_DepthBuffer.srvHandle.IsNull())
		{
			m_ResourceManager->DestroySrv(m_DepthBuffer.srvHandle, false);
		}
		if (!m_DepthBuffer.dsvHandle.IsNull())
		{
			m_ResourceManager->DestroyDsv(m_DepthBuffer.dsvHandle, false);
		}
		if (!m_DepthBuffer.textureHandle.IsNull())
		{
			m_ResourceManager->DestroyTexture(m_DepthBuffer.textureHandle, false);
		}
		m_DepthBuffer = {};

		if (!m_MotionVectors.rtvHandle.IsNull())
		{
			m_ResourceManager->DestroyRtv(m_MotionVectors.rtvHandle, false);
		}
		if (!m_MotionVectors.textureHandle.IsNull())
		{
			m_ResourceManager->DestroyTexture(m_MotionVectors.textureHandle, false);
		}
		m_MotionVectors = {};

		if (!m_SceneColor.srvHandle.IsNull())
		{
			m_ResourceManager->DestroySrv(m_SceneColor.srvHandle, false);
		}
		if (!m_SceneColor.rtvHandle.IsNull())
		{
			m_ResourceManager->DestroyRtv(m_SceneColor.rtvHandle, false);
		}
		if (!m_SceneColor.textureHandle.IsNull())
		{
			m_ResourceManager->DestroyTexture(m_SceneColor.textureHandle, false);
		}
		m_SceneColor = {};

		if (!m_MotionVectorSrv.IsNull())
		{
			m_ResourceManager->DestroySrv(m_MotionVectorSrv, false);
		}
		m_MotionVectorSrv = {};

		if (!m_OutlineMask.srvHandle.IsNull())
		{
			m_ResourceManager->DestroySrv(m_OutlineMask.srvHandle, false);
		}
		if (!m_OutlineMask.rtvHandle.IsNull())
		{
			m_ResourceManager->DestroyRtv(m_OutlineMask.rtvHandle, false);
		}
		if (!m_OutlineMask.textureHandle.IsNull())
		{
			m_ResourceManager->DestroyTexture(m_OutlineMask.textureHandle, false);
		}
		m_OutlineMask = {};
	}

	RenderTargetRef
	CreateBackendRenderTarget(
		const RenderTargetDesc&  desc,
		bgpu::DeviceRef          device,
		bgpu::CommandQueueRef    queue,
		bgpu::ResourceManagerRef resourceManager,
		bool                     enableDebug)
	{
		auto swapchain =
			desc.headless ? nullptr : CreateBackendSwapchain(desc, device, queue, enableDebug);
		return core::SharedRef<RenderTarget>::Make(
			desc,
			std::move(swapchain),
			std::move(device),
			std::move(queue),
			std::move(resourceManager));
	}
}
