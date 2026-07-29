#include "RenderTarget_wgpu.h"

namespace bgl
{
	RenderTarget::RenderTarget(
		const RenderTargetDesc& desc,
		DeviceRef               device,
		CommandQueueRef         queue,
		ResourceManagerRef      resourceManager) :
		m_Device(std::move(device)), m_CommandQueue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager)), m_Width(desc.width), m_Height(desc.height)
	{
		if (!desc.headless)
		{
			gfatal("Windowed render targets are not implemented on the WebGPU backend yet");
		}

		for (auto i = 0u; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i] = m_Device->CreateCommandAllocator(QueueType::kGraphics);
		}

		CreateOffscreenRenderTargets();
	}

	RenderTarget::~RenderTarget() noexcept
	{
		logger::trace("~RenderTarget");

		// Idle the GPU so no in-flight frame still references the backbuffers we free.
		m_CommandQueue->Flush();

		DestroyRenderTargets();

		for (auto i = 0u; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i].Reset();
		}
	}

	void
	RenderTarget::CreateOffscreenRenderTargets()
	{
		for (auto i = 0u; i < c_SwapchainImageCount; i++)
		{
			auto texDesc      = TextureDesc();
			texDesc.width     = static_cast<uint32_t>(m_Width);
			texDesc.height    = static_cast<uint32_t>(m_Height);
			texDesc.debugName = std::format("Offscreen Back Buffer: {}", i);
			texDesc.dimension = TextureDimension::kTexture2D;
			texDesc.format    = Format::SBGRA8_UNORM;
			texDesc.usage     = TextureUsageFlag::kRenderTarget;
			texDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

			m_BackBuffers[i].textureHandle = m_ResourceManager->CreateTexture(texDesc);

			auto rtvDesc      = RtvDesc();
			rtvDesc.format    = Format::SBGRA8_UNORM;
			rtvDesc.debugName = std::format("Offscreen Back Buffer RTV: {}", i);

			m_BackBuffers[i].rtvHandle =
				m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
		}

		{
			auto depthTextureDesc      = TextureDesc();
			depthTextureDesc.format    = Format::D24S8;
			depthTextureDesc.width     = static_cast<uint32_t>(m_Width);
			depthTextureDesc.height    = static_cast<uint32_t>(m_Height);
			depthTextureDesc.dimension = TextureDimension::kTexture2D;
			depthTextureDesc.debugName = "Depth Buffer";
			depthTextureDesc.usage     = TextureUsageFlag::kDepthStencil;

			depthTextureDesc.clearValue.SetDepthStencil(1.0f, 0);

			m_DepthBuffer.textureHandle = m_ResourceManager->CreateTexture(depthTextureDesc);

			auto dsvDesc      = DsvDesc();
			dsvDesc.format    = Format::D24S8;
			dsvDesc.debugName = "Depth Buffer DSV";

			m_DepthBuffer.dsvHandle =
				m_ResourceManager->CreateDsv(m_DepthBuffer.textureHandle, dsvDesc);
		}

		{
			// kSRV as well as kRenderTarget: the buffer exists to be resampled by a later pass.
			auto motionTextureDesc      = TextureDesc();
			motionTextureDesc.format    = Format::RG16_FLOAT;
			motionTextureDesc.width     = static_cast<uint32_t>(m_Width);
			motionTextureDesc.height    = static_cast<uint32_t>(m_Height);
			motionTextureDesc.dimension = TextureDimension::kTexture2D;
			motionTextureDesc.debugName = "Motion Vectors";
			motionTextureDesc.usage =
				TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };

			motionTextureDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));

			m_MotionVectors.textureHandle = m_ResourceManager->CreateTexture(motionTextureDesc);

			auto rtvDesc      = RtvDesc();
			rtvDesc.format    = Format::RG16_FLOAT;
			rtvDesc.debugName = "Motion Vectors RTV";

			m_MotionVectors.rtvHandle =
				m_ResourceManager->CreateRtv(m_MotionVectors.textureHandle, rtvDesc);
		}
	}

	void
	RenderTarget::PresentAndAdvance() noexcept
	{
		// Recorded before advancing: a readback samples the frame that was just presented, not the
		// one about to be recorded.
		m_LastPresentedIndex = m_FrameIndex;

		m_FrameIndex = (m_FrameIndex + 1) % c_SwapchainImageCount;
	}

	void
	RenderTarget::ResizeBackbuffers(uint32_t width, uint32_t height)
	{
		DestroyRenderTargets();

		m_Width  = static_cast<int>(width);
		m_Height = static_cast<int>(height);

		m_FrameIndex = 0;
		CreateOffscreenRenderTargets();

		// The backbuffers these fences described no longer exist, so the next frame must not wait
		// on them.
		for (auto& slotFence : m_FenceValues)
		{
			slotFence = 0;
		}

		m_LastPresentedIndex = m_FrameIndex;
	}

	void
	RenderTarget::DestroyRenderTargets()
	{
		for (auto i = 0u; i < c_SwapchainImageCount; i++)
		{
			m_ResourceManager->DestroyRtv(m_BackBuffers[i].rtvHandle, false);
			m_ResourceManager->DestroyTexture(m_BackBuffers[i].textureHandle, false);
		}

		m_ResourceManager->DestroyDsv(m_DepthBuffer.dsvHandle, false);
		m_ResourceManager->DestroyTexture(m_DepthBuffer.textureHandle, false);

		m_ResourceManager->DestroyRtv(m_MotionVectors.rtvHandle, false);
		m_ResourceManager->DestroyTexture(m_MotionVectors.textureHandle, false);
	}
}
