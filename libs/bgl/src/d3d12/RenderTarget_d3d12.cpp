#include "RenderTarget_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "resource/ResourceManager_d3d12.h"

namespace bgl
{
	RenderTarget::RenderTarget(
		const RenderTargetDesc& desc,
		DeviceRef               device,
		CommandQueueRef         queue,
		ResourceManagerRef      resourceManager,
		bool                    enableDebug) :
		m_Device(std::move(device)), m_CommandQueue(std::move(queue)),
		m_ResourceManager(std::move(resourceManager)), m_Headless(desc.headless),
		m_EnableDebug(enableDebug), m_Wnd(desc.wnd), m_Width(desc.width), m_Height(desc.height)
	{
		for (UINT i = 0; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i] = m_Device->CreateCommandAllocator();
		}

		if (!m_Headless)
		{
			HWND hwnd = m_Wnd ? static_cast<HWND>(m_Wnd) : GetActiveWindow();
			CreateSwapchain(hwnd);
			CreateRenderTargets();
		}
		else
		{
			CreateOffscreenRenderTargets();
		}
	}

	RenderTarget::~RenderTarget() noexcept
	{
		logger::trace("~RenderTarget");

		// Idle the GPU so no in-flight frame still references the backbuffers we free.
		m_CommandQueue->As<CommandQueue>()->Flush();

		if (m_SwapChain)
		{
			m_SwapChain->SetFullscreenState(FALSE, nullptr);
		}

		DestroyRenderTargets(m_CommandQueue->GetNextFenceValue());

		m_SwapChain.Reset();

		for (UINT i = 0; i < c_SwapchainImageCount; i++)
		{
			m_CommandAllocator[i].Reset();
		}
	}

	void
	RenderTarget::CreateSwapchain(HWND hWnd)
	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width                 = static_cast<UINT>(m_Width);
		sd.Height                = static_cast<UINT>(m_Height);
		sd.Format                = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount           = c_SwapchainImageCount;
		sd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling               = DXGI_SCALING_STRETCH;
		sd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
		sd.SampleDesc.Count      = 1;

		wrl::ComPtr<IDXGIFactory4> factory;
		UINT                       factoryFlags = m_EnableDebug ? DXGI_CREATE_FACTORY_DEBUG : 0;
		CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)) >> d3d12ErrChecker;

		wrl::ComPtr<IDXGISwapChain1> swap;

		auto d3d12CommandQueue = m_CommandQueue->As<CommandQueue>()->GetD3D12CommandQueue();
		factory->CreateSwapChainForHwnd(d3d12CommandQueue, hWnd, &sd, nullptr, nullptr, &swap) >>
			d3d12ErrChecker;

		swap->QueryInterface(IID_PPV_ARGS(&m_SwapChain)) >> d3d12ErrChecker;
		factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER) >> d3d12ErrChecker;

		m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	}

	void
	RenderTarget::CreateRenderTargets()
	{
		{
			TextureDesc textureDesc{};
			textureDesc.format        = Format::BGRA8_UNORM;
			textureDesc.width         = static_cast<uint32_t>(m_Width);
			textureDesc.height        = static_cast<uint32_t>(m_Height);
			textureDesc.dimension     = TextureDimension::kTexture2D;
			textureDesc.usage         = TextureUsageFlag::kRenderTarget;
			textureDesc.initialLayout = BarrierLayout::kPresent;

			for (UINT i = 0; i < c_SwapchainImageCount; i++)
			{
				wrl::ComPtr<ID3D12Resource> backBuffer;
				m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)) >> d3d12ErrChecker;

				m_BackBuffers[i].textureHandle =
					m_ResourceManager->As<ResourceManager>()->CreateTexture(
						std::move(backBuffer),
						textureDesc);

				RtvDesc rtvDesc;
				rtvDesc.format    = Format::SBGRA8_UNORM;
				rtvDesc.debugName = std::format("Back Buffer RTV: {}", i);

				m_BackBuffers[i].rtvHandle =
					m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
			}
		}

		{
			auto depthTextureDesc          = TextureDesc();
			depthTextureDesc.format        = Format::D24S8;
			depthTextureDesc.width         = static_cast<uint32_t>(m_Width);
			depthTextureDesc.height        = static_cast<uint32_t>(m_Height);
			depthTextureDesc.dimension     = TextureDimension::kTexture2D;
			depthTextureDesc.debugName     = "Depth Buffer";
			depthTextureDesc.usage         = TextureUsageFlag::kDepthStencil;
			depthTextureDesc.initialLayout = BarrierLayout::kDepthWrite;

			depthTextureDesc.clearValue.SetDepthStencil(1.0f, 0);

			m_DepthBuffer.textureHandle = m_ResourceManager->CreateTexture(depthTextureDesc);

			auto dsvDesc      = DsvDesc();
			dsvDesc.format    = Format::D24S8;
			dsvDesc.debugName = "Depth Buffer RTV";

			m_DepthBuffer.dsvHandle =
				m_ResourceManager->CreateDsv(m_DepthBuffer.textureHandle, dsvDesc);
		}
	}

	void
	RenderTarget::CreateOffscreenRenderTargets()
	{
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
		}

		{
			auto depthTextureDesc          = TextureDesc();
			depthTextureDesc.format        = Format::D24S8;
			depthTextureDesc.width         = static_cast<uint32_t>(m_Width);
			depthTextureDesc.height        = static_cast<uint32_t>(m_Height);
			depthTextureDesc.dimension     = TextureDimension::kTexture2D;
			depthTextureDesc.debugName     = "Depth Buffer";
			depthTextureDesc.usage         = TextureUsageFlag::kDepthStencil;
			depthTextureDesc.initialLayout = BarrierLayout::kDepthWrite;

			depthTextureDesc.clearValue.SetDepthStencil(1.0f, 0);

			m_DepthBuffer.textureHandle = m_ResourceManager->CreateTexture(depthTextureDesc);

			auto dsvDesc      = DsvDesc();
			dsvDesc.format    = Format::D24S8;
			dsvDesc.debugName = "Depth Buffer RTV";

			m_DepthBuffer.dsvHandle =
				m_ResourceManager->CreateDsv(m_DepthBuffer.textureHandle, dsvDesc);
		}
	}

	void
	RenderTarget::DestroyRenderTargets(uint64_t fenceValue)
	{
		for (UINT i = 0; i < c_SwapchainImageCount; i++)
		{
			m_ResourceManager->DestroyRtv(m_BackBuffers[i].rtvHandle, fenceValue, false);
			m_ResourceManager->DestroyTexture(m_BackBuffers[i].textureHandle, fenceValue, false);
		}

		m_ResourceManager->DestroyDsv(m_DepthBuffer.dsvHandle, fenceValue, false);
		m_ResourceManager->DestroyTexture(m_DepthBuffer.textureHandle, fenceValue, false);
	}
}
