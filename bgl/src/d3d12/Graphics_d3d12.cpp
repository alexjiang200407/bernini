#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "passes/Test.h"
#include "resource/ResourceManager_d3d12.h"
#include <bgl/Graphics.h>
#include <core/file/file.h>

namespace fs = std::filesystem;

namespace bgl
{
	struct TextureRtvHandle
	{
		TextureHandle textureHandle;
		RtvHandle     rtvHandle;
	};

	class Graphics : public core::RefCounter<IGraphics>
	{
	public:
		Graphics(const GraphicsOptions&);
		~Graphics() noexcept;

		void
		DrawFrame() override;

		const GraphicsOptions&
		GetOptions() const
		{
			return m_Opts;
		}

	private:
		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

	private:
		UINT m_FrameIndex = 0;

		GraphicsOptions m_Opts;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;
		CommandAllocatorHandle       m_CommandAllocator[c_BufferCount];
		CommandQueueHandle           m_CommandQueue;

		TextureRtvHandle m_BackBuffers[c_BufferCount];
		UINT64           m_FenceValues[c_BufferCount] = { 0, 0 };

		wrl::ComPtr<ID3D12Debug1>   m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue> m_DxgiInfoQueue;

		DeviceHandle          m_Device;
		ResourceManagerHandle m_ResourceManager;
		TestPass              m_TestPass;
	};
}

namespace bgl
{
	Graphics::Graphics(const GraphicsOptions& opts) : m_Opts(opts)
	{
		{
			auto     libraryPath = core::file::getLibraryPath();
			fs::path logPath     = libraryPath.parent_path() / "bgl.log";

			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);

			auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

			log->set_level(logger::level::info);
			log->flush_on(logger::level::info);

			spdlog::set_default_logger(std::move(log));
			spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v"s);

			logger::info("BGL initialized successfully.");
		}

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

		wrl::ComPtr<ID3D12Device> m_D3D12Device;

		D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_D3D12Device)) >>
			d3d12ErrChecker;

		m_Device = core::SharedRef<Device>::Make(m_D3D12Device);

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			m_CommandAllocator[i] = m_Device->CreateCommandAllocator();
		}

		m_CommandQueue    = m_Device->CreateGraphicsCommandQueue();
		m_ResourceManager = m_Device->CreateResourceManager(1000, 1000);

		if (!m_Opts.headless)
		{
			HWND hwnd = m_Opts.wnd ? static_cast<HWND>(m_Opts.wnd) : GetActiveWindow();
			CreateSwapchain(hwnd);
			CreateRenderTargets();
		}

		m_TestPass.Init(m_Device, m_CommandQueue, m_CommandAllocator[0], m_ResourceManager);
	}

	Graphics::~Graphics() noexcept
	{
		uint64_t shutdownFenceValue = m_CommandQueue->GetNextFenceValue();
		auto     rawQueue           = m_CommandQueue->As<CommandQueue>()->GetD3D12CommandQueue();
		auto     rawFence           = m_CommandQueue->As<CommandQueue>()->GetD3D12Fence();

		if (rawQueue && rawFence)
		{
			rawQueue->Signal(rawFence, shutdownFenceValue);
			m_CommandQueue->WaitForFenceCPUBlocking(shutdownFenceValue);
		}

		if (m_SwapChain)
			m_SwapChain->SetFullscreenState(FALSE, nullptr) >> d3d12ErrChecker;

		m_TestPass.Release(m_ResourceManager);

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			m_ResourceManager->DestroyRtv(m_BackBuffers[i].rtvHandle, shutdownFenceValue, false);
			m_ResourceManager->DestroyTexture(
				m_BackBuffers[i].textureHandle,
				shutdownFenceValue,
				false);
		}

		m_ResourceManager.Reset();

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			m_CommandAllocator[i].Reset();
		}

		if (m_SwapChain)
		{
			m_SwapChain->SetFullscreenState(FALSE, nullptr);
			m_SwapChain.Reset();
		}

		m_CommandQueue.Reset();
		m_Device.Reset();

		m_DxgiInfoQueue.Reset();
		m_DebugController.Reset();

		if (m_Opts.enableDebugLayer)
		{
			Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)) >> d3d12ErrChecker;
			dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		}
	}

	void
	Graphics::DrawFrame()
	{
		gassert(m_Opts.headless == false, "Cannot Draw Frame when in headless mode");

		uint64_t fenceToWaitOn = m_FenceValues[m_FrameIndex];
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(fenceToWaitOn);
		}

		m_CommandAllocator[m_FrameIndex]->ResetAllocator();

		auto frameBuffer = FrameBuffer();
		frameBuffer.AddColorAttachment(m_BackBuffers[m_FrameIndex].rtvHandle);

		auto vp = Viewport(m_Opts.width, m_Opts.height);

		m_FenceValues[m_FrameIndex] =
			m_TestPass.Execute(m_CommandQueue, m_CommandAllocator[m_FrameIndex], frameBuffer, vp);

		m_SwapChain->Present(1, 0) >> d3d12ErrChecker;

		uint64_t currentGPUProgress = m_CommandQueue->PollCurrentFenceValue();
		m_ResourceManager->CleanupExpiredResources(currentGPUProgress);

		m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	}

	void
	Graphics::CreateSwapchain(HWND hWnd)
	{
		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width                 = static_cast<UINT>(m_Opts.width);
		sd.Height                = static_cast<UINT>(m_Opts.height);
		sd.Format                = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.BufferCount           = c_BufferCount;
		sd.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.Scaling               = DXGI_SCALING_STRETCH;
		sd.AlphaMode             = DXGI_ALPHA_MODE_IGNORE;
		sd.SampleDesc.Count      = 1;

		wrl::ComPtr<IDXGIFactory4> factory;
		UINT                       factoryFlags = m_DebugController ? DXGI_CREATE_FACTORY_DEBUG : 0;
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
	Graphics::CreateRenderTargets()
	{
		TextureDesc textureDesc{};

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			wrl::ComPtr<ID3D12Resource> backBuffer;
			m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)) >> d3d12ErrChecker;

			m_BackBuffers[i].textureHandle =
				m_ResourceManager->As<ResourceManager>()->CreateTexture(
					std::move(backBuffer),
					textureDesc);

			RtvDesc rtvDesc;
			rtvDesc.format    = Format::BGRA8_UNORM;
			rtvDesc.debugName = std::format("Back Buffer RTV: {}", i);

			m_BackBuffers[i].rtvHandle =
				m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
		}
	}

	GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
