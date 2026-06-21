#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "gfx/GraphicsBase.h"
#include "passes/GBuffer.h"
#include "resource/ResourceManager_d3d12.h"
#include "scene/Scene.h"
#include <core/file/file.h>

namespace fs = std::filesystem;

namespace bgl
{
	class IScene;

	struct TextureRtvHandle
	{
		TextureHandle textureHandle;
		RtvHandle     rtvHandle;
	};

	struct TextureDsvHandle
	{
		TextureHandle textureHandle;
		DsvHandle     dsvHandle;
	};

	class Graphics : public core::RefCounter<GraphicsBase>
	{
	public:
		Graphics(const GraphicsOptions&);
		~Graphics() noexcept;

		Graphics(const Graphics&) noexcept = delete;
		Graphics(Graphics&&) noexcept      = delete;

		Graphics&
		operator=(const Graphics&) noexcept = delete;

		Graphics&
		operator=(Graphics&&) noexcept = delete;

		void
		DrawFrame(IScene* scene) override;

		const GraphicsOptions&
		GetOptions() const
		{
			return m_Opts;
		}

		IDevice*
		GetDevice() const override
		{
			return m_Device.Get();
		}

		core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const override
		{
			return m_ResourceManager.Get();
		}

		SceneHandle
		CreateScene(SceneDesc desc) override
		{
			return core::SharedRef<Scene>::Make(std::move(desc), m_ResourceManager);
		}

	private:
		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

		void
		CreateOffscreenRenderTargets();

	private:
		UINT                                 m_FrameIndex = 0;
		Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;

		GraphicsOptions m_Opts;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		DeviceHandle           m_Device;
		CommandAllocatorHandle m_CommandAllocator[c_BufferCount];
		CommandQueueHandle     m_CommandQueue;
		CommandListHandle      m_CommandList;

		TextureRtvHandle m_BackBuffers[c_BufferCount];
		TextureDsvHandle m_DepthBuffer;
		UINT64           m_FenceValues[c_BufferCount] = { 0, 0 };

		wrl::ComPtr<ID3D12Debug1>   m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue> m_DxgiInfoQueue;

		ResourceManagerHandle m_ResourceManager;
		GBufferPass           m_GBuffer;
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

			log->set_level(static_cast<logger::level::level_enum>(opts.logLevel));
			log->flush_on(static_cast<logger::level::level_enum>(opts.logLevel));

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

		slang::createGlobalSession(m_SlangGlobalSession.writeRef());

		m_Device = core::SharedRef<Device>::Make(m_D3D12Device, m_SlangGlobalSession.get());

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			m_CommandAllocator[i] = m_Device->CreateCommandAllocator();
		}

		m_CommandQueue = m_Device->CreateGraphicsCommandQueue();

		{
			auto resourceManagerDesc          = ResourceManagerDesc();
			resourceManagerDesc.maxCbvSrvUavs = 1000;
			resourceManagerDesc.maxDsvs       = 5;
			resourceManagerDesc.maxRtvs       = 5;
			resourceManagerDesc.maxTextures   = 1000;

			m_ResourceManager = m_Device->CreateResourceManager(resourceManagerDesc);
		}

		CommandListDesc cmdListDesc;
		cmdListDesc.type = QueueType::kGraphics;

		m_CommandList =
			m_Device->CreateCommandList(cmdListDesc, m_CommandAllocator[0], m_ResourceManager);

		if (!m_Opts.headless)
		{
			HWND hwnd = m_Opts.wnd ? static_cast<HWND>(m_Opts.wnd) : GetActiveWindow();
			CreateSwapchain(hwnd);
			CreateRenderTargets();
		}
		else
		{
			CreateOffscreenRenderTargets();
		}

		m_GBuffer.Init(m_Device);
	}

	Graphics::~Graphics() noexcept
	{
		logger::trace("~Graphics");

		auto     cmdQueue           = m_CommandQueue->As<CommandQueue>();
		uint64_t shutdownFenceValue = m_CommandQueue->GetNextFenceValue();
		auto     rawQueue           = cmdQueue->GetD3D12CommandQueue();
		auto     rawFence           = cmdQueue->GetD3D12Fence();

		if (rawQueue && rawFence)
		{
			rawQueue->Signal(rawFence, shutdownFenceValue);
			m_CommandQueue->WaitForFenceCPUBlocking(shutdownFenceValue);
		}

		if (m_SwapChain)
			m_SwapChain->SetFullscreenState(FALSE, nullptr) >> d3d12ErrChecker;

		m_GBuffer.Release();

		for (UINT i = 0; i < c_BufferCount; i++)
		{
			m_ResourceManager->DestroyRtv(m_BackBuffers[i].rtvHandle, shutdownFenceValue, false);
			m_ResourceManager->DestroyTexture(
				m_BackBuffers[i].textureHandle,
				shutdownFenceValue,
				false);
		}

		m_ResourceManager->DestroyDsv(m_DepthBuffer.dsvHandle, shutdownFenceValue, false);
		m_ResourceManager->DestroyTexture(m_DepthBuffer.textureHandle, shutdownFenceValue, false);

		m_CommandList.Reset();
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

		m_SlangGlobalSession.setNull();

		if (m_Opts.enableDebugLayer)
		{
			Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)) >> d3d12ErrChecker;
			dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		}
	}

	void
	Graphics::DrawFrame(IScene* scene)
	{
		uint64_t fenceToWaitOn = m_FenceValues[m_FrameIndex];
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(fenceToWaitOn);
		}

		auto scene_ = scene->As<Scene>();

		m_CommandAllocator[m_FrameIndex]->ResetAllocator();

		m_CommandList->Open(m_CommandQueue.Get(), m_CommandAllocator[m_FrameIndex].Get());

		auto frameBuffer = FrameBuffer();
		frameBuffer.AddColorAttachment(m_BackBuffers[m_FrameIndex].rtvHandle);
		frameBuffer.SetDepthAttachment(m_DepthBuffer.dsvHandle);

		// Clear Framebuffers
		{
			m_ResourceManager->ClearDsv(m_CommandList, m_DepthBuffer.dsvHandle, 1.0f, 0);

			{
				auto barrierDesc = TextureBarrierDesc();
				barrierDesc.AddAccessBefore(BarrierAccessFlag::kNone)
					.AddSyncBefore(BarrierSyncFlag::kNone)
					.SetLayoutBefore(BarrierLayout::kPresent)

					.AddAccessAfter(BarrierAccessFlag::kRenderTarget)
					.AddSyncAfter(BarrierSyncFlag::kRenderTarget)
					.SetLayoutAfter(BarrierLayout::kRenderTarget);

				m_CommandList->Barrier(frameBuffer.colorAttachments[0], barrierDesc);
			}

			{
				float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				m_ResourceManager->ClearRtv(
					m_CommandList,
					m_BackBuffers[m_FrameIndex].rtvHandle,
					clearColor);
			}
		}

		if (scene_->IsFirstFrame())
		{
			scene_->TransitionAll(
				m_CommandList,
				EntryBufferState::kNone,
				EntryBufferState::kUpdate);

			scene_->TransitionAll(
				m_CommandList,
				RangeBufferState::kNone,
				RangeBufferState::kUpdate);
		}

		scene_->Update(m_CommandList);

		scene_->TransitionAll(m_CommandList, EntryBufferState::kUpdate, EntryBufferState::kShader);
		scene_->TransitionAll(m_CommandList, RangeBufferState::kUpdate, RangeBufferState::kShader);

		auto vp = Viewport(static_cast<float>(m_Opts.width), static_cast<float>(m_Opts.height));

		m_GBuffer.Execute(scene_, m_CommandQueue, m_CommandList, frameBuffer, vp);

		{
			auto barrierDesc = TextureBarrierDesc();
			barrierDesc.AddAccessBefore(BarrierAccessFlag::kRenderTarget)
				.AddSyncBefore(BarrierSyncFlag::kRenderTarget)
				.SetLayoutBefore(BarrierLayout::kRenderTarget)

				.AddAccessAfter(BarrierAccessFlag::kNone)
				.AddSyncAfter(BarrierSyncFlag::kNone)
				.SetLayoutAfter(BarrierLayout::kPresent);

			m_CommandList->Barrier(frameBuffer.colorAttachments[0], barrierDesc);
		}

		// For next frame
		scene_->TransitionAll(m_CommandList, EntryBufferState::kShader, EntryBufferState::kUpdate);
		scene_->TransitionAll(m_CommandList, RangeBufferState::kShader, RangeBufferState::kUpdate);

		m_CommandList->Close();

		m_FenceValues[m_FrameIndex] = m_CommandQueue->ExecuteCommandList(m_CommandList);

		if (!m_Opts.headless)
		{
			m_SwapChain->Present(1, 0) >> d3d12ErrChecker;
		}

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
		{
			TextureDesc textureDesc{};
			textureDesc.format       = Format::BGRA8_UNORM;
			textureDesc.width        = static_cast<uint32_t>(m_Opts.width);
			textureDesc.height       = static_cast<uint32_t>(m_Opts.height);
			textureDesc.dimension    = TextureDimension::kTexture2D;
			textureDesc.usage        = TextureUsageFlag::kRenderTarget;
			textureDesc.initalLayout = BarrierLayout::kPresent;

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

		{
			auto depthTextureDesc         = TextureDesc();
			depthTextureDesc.format       = Format::D24S8;
			depthTextureDesc.width        = static_cast<uint32_t>(m_Opts.width);
			depthTextureDesc.height       = static_cast<uint32_t>(m_Opts.height);
			depthTextureDesc.dimension    = TextureDimension::kTexture2D;
			depthTextureDesc.debugName    = "Depth Buffer";
			depthTextureDesc.usage        = TextureUsageFlag::kDepthStencil;
			depthTextureDesc.initalLayout = BarrierLayout::kDepthWrite;

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
	Graphics::CreateOffscreenRenderTargets()
	{
		{
			for (auto i = 0u; i < c_BufferCount; i++)
			{
				auto texDesc      = TextureDesc();
				texDesc.width     = static_cast<uint32_t>(m_Opts.width);
				texDesc.height    = static_cast<uint32_t>(m_Opts.height);
				texDesc.debugName = std::format("Offscreen Back Buffer: {}", i);
				texDesc.dimension = TextureDimension::kTexture2D;
				texDesc.format    = Format::BGRA8_UNORM;
				texDesc.usage     = TextureUsageFlag::kRenderTarget;
				texDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

				m_BackBuffers[i].textureHandle = m_ResourceManager->CreateTexture(texDesc);

				auto rtvDesc      = RtvDesc();
				rtvDesc.format    = Format::BGRA8_UNORM;
				rtvDesc.debugName = std::format("Offscreen Back Buffer RTV: {}", i);

				m_BackBuffers[i].rtvHandle =
					m_ResourceManager->CreateRtv(m_BackBuffers[i].textureHandle, rtvDesc);
			}
		}

		{
			auto depthTextureDesc         = TextureDesc();
			depthTextureDesc.format       = Format::D24S8;
			depthTextureDesc.width        = static_cast<uint32_t>(m_Opts.width);
			depthTextureDesc.height       = static_cast<uint32_t>(m_Opts.height);
			depthTextureDesc.dimension    = TextureDimension::kTexture2D;
			depthTextureDesc.debugName    = "Depth Buffer";
			depthTextureDesc.usage        = TextureUsageFlag::kDepthStencil;
			depthTextureDesc.initalLayout = BarrierLayout::kDepthWrite;

			depthTextureDesc.clearValue.SetDepthStencil(1.0f, 0);

			m_DepthBuffer.textureHandle = m_ResourceManager->CreateTexture(depthTextureDesc);

			auto dsvDesc      = DsvDesc();
			dsvDesc.format    = Format::D24S8;
			dsvDesc.debugName = "Depth Buffer RTV";

			m_DepthBuffer.dsvHandle =
				m_ResourceManager->CreateDsv(m_DepthBuffer.textureHandle, dsvDesc);
		}
	}

	GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
