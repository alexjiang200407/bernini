#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "passes/Test.h"
#include "resource/ResourceManager_d3d12.h"
#include <bgl/bgl.h>
#include <core/file/file.h>

namespace bgl
{
	struct TextureRtvHandle
	{
		TextureHandle textureHandle;
		RtvHandle     rtvHandle;
	};

	class GraphicsImpl
	{
	public:
		GraphicsImpl(const GraphicsOptions&);
		~GraphicsImpl() noexcept;

		void
		DrawFrame();

	private:
		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

	private:
		static constexpr UINT m_BufferCount = 2;
		UINT                  m_FrameIndex  = 0;

		GraphicsOptions m_Opts;

		wrl::ComPtr<ID3D12Device>    m_D3D12Device;
		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		CommandAllocator m_CommandAllocator[m_BufferCount];
		CommandQueue     m_CommandQueue;

		TextureRtvHandle m_BackBuffers[m_BufferCount];
		UINT64           m_FenceValues[m_BufferCount] = { 0, 0 };

		wrl::ComPtr<ID3D12Debug1>   m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue> m_DxgiInfoQueue;

		Device          m_Device;
		ResourceManager m_ResourceManager;
		TestPass        m_TestPass;
	};
}

namespace bgl
{
	GraphicsImpl::GraphicsImpl(const GraphicsOptions& opts) : m_Opts(opts)
	{
		if (m_Opts.enablePixDebug)
		{
			LoadLibraryA("WinPixGpuCapturer.dll");
		}

		if (m_Opts.enableDebugLayer)
		{
			D3D12GetDebugInterface(IID_PPV_ARGS(&m_DebugController)) >> d3d12ErrChecker;
			m_DebugController->EnableDebugLayer();
			if (m_Opts.enableGPUValidationLayer)
			{
				m_DebugController->SetEnableGPUBasedValidation(TRUE);
			}

			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_DxgiInfoQueue)) >> d3d12ErrChecker;

			m_DxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL,
				DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
				TRUE);

			m_DxgiInfoQueue->SetBreakOnSeverity(
				DXGI_DEBUG_ALL,
				DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
				TRUE);
		}

		D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_D3D12Device)) >>
			d3d12ErrChecker;

		m_Device.EmplaceImpl(m_D3D12Device);

		for (UINT i = 0; i < m_BufferCount; i++)
		{
			m_CommandAllocator[i] = m_Device.CreateCommandAllocator();
		}

		m_CommandQueue    = m_Device.CreateCommandQueue();
		m_ResourceManager = m_Device.CreateResourceManager(1000, 1000);

		if (!m_Opts.headless)
		{
			HWND hwnd = m_Opts.wnd ? static_cast<HWND>(m_Opts.wnd) : GetActiveWindow();
			CreateSwapchain(hwnd);
			CreateRenderTargets();
		}

		m_TestPass.Init(m_Device, m_CommandQueue, m_CommandAllocator[0], m_ResourceManager);
	}

	GraphicsImpl::~GraphicsImpl() noexcept
	{
		if (m_CommandQueue.IsInitialized())
		{
			uint64_t lastSubmittedFence = m_CommandQueue.GetNextFenceValue() - 1;
			m_CommandQueue.WaitForFenceCPUBlocking(lastSubmittedFence);
		}

		if (m_SwapChain)
			m_SwapChain->SetFullscreenState(FALSE, nullptr) >> d3d12ErrChecker;
	}

	void
	GraphicsImpl::DrawFrame()
	{
		gassert(m_Opts.headless == false, "Cannot Draw Frame when in headless mode");

		uint64_t fenceToWaitOn = m_FenceValues[m_FrameIndex];
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue.WaitForFenceCPUBlocking(fenceToWaitOn);
		}

		m_CommandAllocator[m_FrameIndex].Reset();

		auto frameBuffer = FrameBuffer();
		frameBuffer.AddColorAttachment(m_BackBuffers[m_FrameIndex].rtvHandle);

		auto vp = Viewport(m_Opts.width, m_Opts.height);

		m_FenceValues[m_FrameIndex] =
			m_TestPass.Execute(m_CommandQueue, m_CommandAllocator[m_FrameIndex], frameBuffer, vp);

		m_SwapChain->Present(1, 0) >> d3d12ErrChecker;

		uint64_t currentGPUProgress = m_CommandQueue.PollCurrentFenceValue();
		m_ResourceManager.CleanupExpiredResources(currentGPUProgress);

		m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	}

	void
	GraphicsImpl::CreateSwapchain(HWND hWnd)
	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width                 = static_cast<UINT>(m_Opts.width);
		sd.Height                = static_cast<UINT>(m_Opts.height);
		sd.Format                = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount           = m_BufferCount;
		sd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling               = DXGI_SCALING_STRETCH;
		sd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
		sd.SampleDesc.Count      = 1;

		wrl::ComPtr<IDXGIFactory4> factory;
		UINT                       factoryFlags = m_DebugController ? DXGI_CREATE_FACTORY_DEBUG : 0;
		CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)) >> d3d12ErrChecker;

		wrl::ComPtr<IDXGISwapChain1> swap;

		factory->CreateSwapChainForHwnd(
			m_CommandQueue->GetD3D12CommandQueue(),
			hWnd,
			&sd,
			nullptr,
			nullptr,
			&swap) >>
			d3d12ErrChecker;

		swap->QueryInterface(IID_PPV_ARGS(&m_SwapChain)) >> d3d12ErrChecker;
		factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER) >> d3d12ErrChecker;

		m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	}

	void
	GraphicsImpl::CreateRenderTargets()
	{
		TextureDesc textureDesc{};

		for (UINT i = 0; i < m_BufferCount; i++)
		{
			wrl::ComPtr<ID3D12Resource> backBuffer;
			m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)) >> d3d12ErrChecker;

			m_BackBuffers[i].textureHandle =
				m_ResourceManager->CreateTexture(std::move(backBuffer), textureDesc);

			RtvDesc rtvDesc;
			rtvDesc.format    = Format::BGRA8_UNORM;
			rtvDesc.debugName = std::format("Back Buffer RTV: {}", i);

			m_BackBuffers[i].rtvHandle =
				m_ResourceManager.CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
		}
	}

	Graphics::Graphics(const GraphicsOptions& opts) : PImpl<GraphicsImpl>(opts) {}

	Graphics::~Graphics() noexcept = default;

	void
	Graphics::DrawFrame() const
	{
		gassert(IsInitialized(), "Graphics implementation is not initialized.");
		GetImpl()->DrawFrame();
	}
}
