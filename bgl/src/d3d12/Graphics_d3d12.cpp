#include "RenderTarget_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/CommandQueue_d3d12.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "fg/FrameGraph.h"
#include "gfx/GraphicsBase.h"
#include "passes/ClearPass.h"
#include "passes/CompactInstancesPass.h"
#include "passes/DrawData.h"
#include "passes/ForwardPass.h"
#include "passes/PreparePresentPass.h"
#include "resource/ResourceManager_d3d12.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <DirectXTex.h>
#include <bgl/RenderContext.h>
#include <core/file/file.h>

namespace fs = std::filesystem;

namespace bgl
{
	class IScene;

	// Graph resource name of the active target's backbuffer.
	constexpr std::string_view c_BackbufferName = "backbuffer";

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
		BeginFrame(const RenderTargetHandle& target) override;

		void
		Draw(const RenderContext& context) override;

		void
		EndFrame() override;

		void
		Resize(const RenderTargetHandle& target, uint32_t width, uint32_t height) override;

		void
		ScreenshotRaw(const RenderTargetHandle& target, const std::string& filepath) override;

		const GraphicsOptions&
		GetOptions() const
		{
			return m_Opts;
		}

		IDevice*
		GetDevice() const noexcept override
		{
			return m_Device.Get();
		}

		core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const noexcept override
		{
			return m_ResourceManager.Get();
		}

		SceneHandle
		CreateScene(SceneDesc desc) override
		{
			return core::SharedRef<Scene>::Make(std::move(desc), m_ResourceManager);
		}

		SceneViewHandle
		CreateSceneView(const SceneHandle& scene, uint32_t maxInstances) override
		{
			return core::SharedRef<SceneView>::Make(scene, maxInstances, m_ResourceManager);
		}

		RenderTargetHandle
		CreateRenderTarget(const RenderTargetDesc& desc) override
		{
			return core::SharedRef<RenderTarget>::Make(
				desc,
				m_Device,
				m_CommandQueue,
				m_ResourceManager,
				m_Opts.enableDebugLayer);
		}

	private:
		// Forwards debug-layer / GPU-based-validation messages to the spdlog log.
		static void CALLBACK
		LogD3D12Message(
			D3D12_MESSAGE_CATEGORY category,
			D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID       id,
			LPCSTR                 description,
			void*                  context);

	private:
		Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;

		GraphicsOptions m_Opts;

		DeviceHandle       m_Device;
		CommandQueueHandle m_CommandQueue;
		CommandListHandle  m_CommandList;

		// Allocator used only to construct m_CommandList; per-frame recording uses the
		// active target's own allocator ring.
		CommandAllocatorHandle m_BootstrapAllocator;

		bool m_FrameActive = false;

		// The render target bound by the current BeginFrame (null outside a frame).
		RenderTarget* m_ActiveTarget = nullptr;

		FrameGraph m_FrameGraph;
		uint32_t   m_DrawCount = 0;

		wrl::ComPtr<ID3D12Debug1>     m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue>   m_DxgiInfoQueue;
		wrl::ComPtr<ID3D12InfoQueue1> m_D3D12InfoQueue;
		DWORD                         m_MessageCallbackCookie = 0;

		ResourceManagerHandle m_ResourceManager;
		PreparePresentPass    m_PreparePresentPass;
		ForwardPass           m_Forward;
		CompactInstancesPass  m_CompactInstances;
	};
}

namespace bgl
{
	Graphics::Graphics(const GraphicsOptions& opts) : m_Opts(opts)
	{
		{
			auto     libraryPath = core::file::getLibraryPath();
			fs::path logPath     = libraryPath.parent_path() / "bgl.log";

			// Truncate once per process so a single run accumulates every device's
			// messages instead of each new Graphics clobbering the previous log.
			static bool s_logTruncated = false;
			const bool  truncate       = !s_logTruncated;
			s_logTruncated             = true;

			auto sink =
				std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), truncate);

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

		m_Device = core::SharedRef<Device>::Make(m_D3D12Device, m_SlangGlobalSession);

		// Route debug-layer and GPU-based-validation messages (which otherwise only
		// reach an attached debugger) into the spdlog log.
		if (m_Opts.enableDebugLayer && SUCCEEDED(m_D3D12Device.As(&m_D3D12InfoQueue)))
		{
			m_D3D12InfoQueue->RegisterMessageCallback(
				&Graphics::LogD3D12Message,
				D3D12_MESSAGE_CALLBACK_FLAG_NONE,
				this,
				&m_MessageCallbackCookie) >>
				d3d12ErrChecker;
		}

		m_BootstrapAllocator = m_Device->CreateCommandAllocator();

		m_CommandQueue = m_Device->CreateGraphicsCommandQueue();

		{
			auto resourceManagerDesc          = ResourceManagerDesc();
			resourceManagerDesc.maxCbvSrvUavs = m_Opts.maxCbvSrvUavs;
			resourceManagerDesc.maxDsvs       = m_Opts.maxDsvs;
			resourceManagerDesc.maxRtvs       = m_Opts.maxRtvs;
			resourceManagerDesc.maxTextures   = m_Opts.maxTextures;

			m_ResourceManager = m_Device->CreateResourceManager(resourceManagerDesc);
		}

		CommandListDesc cmdListDesc;
		cmdListDesc.type = QueueType::kGraphics;

		m_CommandList =
			m_Device->CreateCommandList(cmdListDesc, m_BootstrapAllocator, m_ResourceManager);

		m_CompactInstances.Init(m_Device, m_ResourceManager);
		m_Forward.Init(m_Device);
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

		m_Forward.Release();
		m_CompactInstances.Release(shutdownFenceValue, false);

		// Clear retained passes; each pass descriptor holds a resource-manager reference
		// that would otherwise keep the manager alive past the live-object report.
		m_FrameGraph.Reset();

		m_CommandList.Reset();
		m_ResourceManager.Reset();

		m_BootstrapAllocator.Reset();

		m_CommandQueue.Reset();
		m_Device.Reset();

		m_DxgiInfoQueue.Reset();
		m_DebugController.Reset();
		m_SlangGlobalSession.setNull();

		if (m_D3D12InfoQueue && m_MessageCallbackCookie != 0)
		{
			m_D3D12InfoQueue->UnregisterMessageCallback(m_MessageCallbackCookie);
		}
		m_D3D12InfoQueue.Reset();

		if (m_Opts.enableDebugLayer)
		{
			wrl::ComPtr<IDXGIDebug1> dxgiDebug;
			DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)) >> d3d12ErrChecker;
			dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		}
	}

	void
	Graphics::BeginFrame(const RenderTargetHandle& target)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("BeginFrame called while a frame is already active");
		}

		m_ActiveTarget = target->As<RenderTarget>();
		gassert(m_ActiveTarget != nullptr, "BeginFrame requires a valid RenderTarget");

		RenderTarget& rt    = *m_ActiveTarget;
		const UINT    index = rt.m_FrameIndex;

		uint64_t fenceToWaitOn = rt.m_FenceValues[index];
		if (fenceToWaitOn != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(fenceToWaitOn);
		}

		rt.m_CommandAllocator[index]->ResetAllocator();

		m_CommandList->Open(m_CommandQueue.Get(), rt.m_CommandAllocator[index].Get());

		m_FrameGraph.Reset();
		m_DrawCount = 0;
		m_FrameGraph.RegisterQueue("main", m_CommandQueue, m_CommandList);
		m_FrameGraph.ImportTexture(
			std::string(c_BackbufferName),
			rt.m_BackBuffers[index].textureHandle,
			AccessState{ BarrierSyncFlag::kNone,
		                 BarrierAccessFlag::kNone,
		                 BarrierLayout::kPresent });

		const std::array<ClearPass::ColorTarget, 1> colorTargets{
			{ { std::string(c_BackbufferName),
			    rt.m_BackBuffers[index].rtvHandle,
			    { 0.0f, 0.0f, 0.0f, 1.0f } } }
		};
		ClearPass().AttachToFrameGraph(
			m_FrameGraph,
			m_ResourceManager.Get(),
			colorTargets,
			rt.m_DepthBuffer.dsvHandle);

		m_FrameActive = true;
	}

	void
	Graphics::Draw(const RenderContext& context)
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("Draw must be called between BeginFrame and EndFrame");
		}

		if (context.view == nullptr)
		{
			throw GraphicsError("RenderContext passed to Draw requires a SceneView");
		}

		auto       view_    = context.view->As<SceneView>();
		auto       scene_   = view_->GetScene()->As<Scene>();
		const auto viewport = context.viewport;
		const auto viewProj = context.camera.GetViewProjection();

		const uint32_t drawIdx = m_DrawCount++;

		m_FrameGraph.SetResourceNamespace(view_->ResourceNamespace());

		scene_->AttachToFrameGraph(m_FrameGraph, drawIdx);
		view_->AttachToFrameGraph(m_FrameGraph, drawIdx);

		auto draw     = DrawData();
		draw.drawIdx  = drawIdx;
		draw.view     = context.view;
		draw.viewport = viewport;
		draw.viewProj = viewProj;
		draw.backBufferHandle =
			m_ActiveTarget->m_BackBuffers[m_ActiveTarget->m_FrameIndex].rtvHandle;
		draw.depthBufferHandle = m_ActiveTarget->m_DepthBuffer.dsvHandle;
		draw.backBufferName    = std::string(c_BackbufferName);

		m_CompactInstances.AttachToFrameGraph(m_FrameGraph, draw);
		m_Forward.AttachToFrameGraph(m_FrameGraph, draw);
	}

	void
	Graphics::EndFrame()
	{
		if (!m_FrameActive)
		{
			throw GraphicsError("EndFrame called without a matching BeginFrame");
		}

		RenderTarget& rt    = *m_ActiveTarget;
		const UINT    index = rt.m_FrameIndex;

		m_FrameGraph.SetResourceNamespace("");
		m_PreparePresentPass.AttachToFrameGraph(m_FrameGraph, std::string(c_BackbufferName));

		m_FrameGraph.Compile(m_ResourceManager.Get());
		m_FrameGraph.Execute();

		m_CommandList->Close();

		rt.m_FenceValues[index] = m_CommandQueue->ExecuteCommandList(m_CommandList);

		if (!rt.m_Headless)
		{
			rt.m_SwapChain->Present(1, 0) >> d3d12ErrChecker;
		}

		uint64_t currentGPUProgress = m_CommandQueue->PollCurrentFenceValue();
		m_ResourceManager->CleanupExpiredResources(currentGPUProgress);

		// Remember which backbuffer just received the frame before advancing.
		rt.m_LastPresentedIndex = index;

		if (rt.m_Headless)
		{
			rt.m_FrameIndex = (index + 1) % c_BufferCount;
		}
		else
		{
			rt.m_FrameIndex = rt.m_SwapChain->GetCurrentBackBufferIndex();
		}

		m_ActiveTarget = nullptr;
		m_FrameActive  = false;
	}

	void
	Graphics::Resize(const RenderTargetHandle& target, uint32_t width, uint32_t height)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("Resize cannot be called between BeginFrame and EndFrame");
		}

		if (width == 0 || height == 0)
		{
			throw GraphicsError("Resize dimensions must be non-zero");
		}

		RenderTarget& rt = *target->As<RenderTarget>();

		if (static_cast<uint32_t>(rt.m_Width) == width &&
		    static_cast<uint32_t>(rt.m_Height) == height)
		{
			return;
		}

		// Idle the GPU so no in-flight frame still references the render targets we
		// are about to release.
		m_CommandQueue->As<CommandQueue>()->Flush();

		// Reset the command list so it drops its references to the old backbuffers;
		// the swap chain cannot be resized while any reference to them is alive.
		m_BootstrapAllocator->ResetAllocator();
		m_CommandList->Open(m_CommandQueue.Get(), m_BootstrapAllocator.Get());
		m_CommandList->Close();

		rt.DestroyRenderTargets(m_CommandQueue->GetNextFenceValue());

		rt.m_Width  = static_cast<int>(width);
		rt.m_Height = static_cast<int>(height);

		if (!rt.m_Headless)
		{
			rt.m_SwapChain
					->ResizeBuffers(c_BufferCount, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0) >>
				d3d12ErrChecker;

			rt.m_FrameIndex = rt.m_SwapChain->GetCurrentBackBufferIndex();
			rt.CreateRenderTargets();
		}
		else
		{
			rt.m_FrameIndex = 0;
			rt.CreateOffscreenRenderTargets();
		}

		// The GPU is idle and the backbuffers were re-indexed: drop the stale
		// per-frame fences so the next BeginFrame does not wait on them.
		for (auto& fenceValue : rt.m_FenceValues)
		{
			fenceValue = 0;
		}

		rt.m_LastPresentedIndex = rt.m_FrameIndex;
	}

	void CALLBACK
	Graphics::LogD3D12Message(
		D3D12_MESSAGE_CATEGORY /*category*/,
		D3D12_MESSAGE_SEVERITY severity,
		D3D12_MESSAGE_ID /*id*/,
		LPCSTR description,
		void*  context)
	{
		bool severe = false;
		switch (severity)
		{
		case D3D12_MESSAGE_SEVERITY_CORRUPTION:
		case D3D12_MESSAGE_SEVERITY_ERROR:
			logger::error("[D3D12] {}", description);
			severe = true;
			break;
		case D3D12_MESSAGE_SEVERITY_WARNING:
			logger::warn("[D3D12] {}", description);
			severe = true;
			break;
		case D3D12_MESSAGE_SEVERITY_INFO:
			logger::info("[D3D12] {}", description);
			break;
		case D3D12_MESSAGE_SEVERITY_MESSAGE:
		default:
			logger::debug("[D3D12] {}", description);
			break;
		}

		const auto* self = static_cast<const Graphics*>(context);
		if (severe && self != nullptr && self->m_Opts.strictError)
		{
			gfatal("[D3D12] strict error: {}", description);
		}
	}

	void
	Graphics::ScreenshotRaw(const RenderTargetHandle& target, const std::string& filepath)
	{
		if (m_FrameActive)
		{
			throw GraphicsError("ScreenshotRaw cannot be called between BeginFrame and EndFrame");
		}

		RenderTarget& rt = *target->As<RenderTarget>();

		const UINT    index         = rt.m_LastPresentedIndex;
		TextureHandle textureHandle = rt.m_BackBuffers[index].textureHandle;

		// Make sure the frame that produced this backbuffer has finished.
		if (rt.m_FenceValues[index] != 0)
		{
			m_CommandQueue->WaitForFenceCPUBlocking(rt.m_FenceValues[index]);
		}

		auto layout = m_ResourceManager->GetTextureReadbackLayout(textureHandle);

		auto readbackDesc      = ReadbackBufferDesc();
		readbackDesc.byteSize  = layout.totalBytes;
		readbackDesc.debugName = "ScreenshotRaw Readback";

		auto readback = m_ResourceManager->CreateReadbackBuffer(readbackDesc);

		rt.m_CommandAllocator[index]->ResetAllocator();
		m_CommandList->Open(m_CommandQueue.Get(), rt.m_CommandAllocator[index].Get());

		{
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kNone)
				.AddAccessBefore(BarrierAccessFlag::kNone)
				.SetLayoutBefore(BarrierLayout::kPresent)
				.AddSyncAfter(BarrierSyncFlag::kCopy)
				.AddAccessAfter(BarrierAccessFlag::kCopySource)
				.SetLayoutAfter(BarrierLayout::kCopySource);
			m_CommandList->Barrier(textureHandle, barrier);
		}

		m_CommandList->CopyTextureToReadback(readback, textureHandle);

		{
			auto barrier = TextureBarrierDesc();
			barrier.AddSyncBefore(BarrierSyncFlag::kCopy)
				.AddAccessBefore(BarrierAccessFlag::kCopySource)
				.SetLayoutBefore(BarrierLayout::kCopySource)
				.AddSyncAfter(BarrierSyncFlag::kNone)
				.AddAccessAfter(BarrierAccessFlag::kNone)
				.SetLayoutAfter(BarrierLayout::kPresent);
			m_CommandList->Barrier(textureHandle, barrier);
		}

		m_CommandList->Close();

		uint64_t fence = m_CommandQueue->ExecuteCommandList(m_CommandList);
		m_CommandQueue->WaitForFenceCPUBlocking(fence);

		// Wrap the mapped readback as a DirectX::Image (the padded rowPitch is fine,
		// DirectX::Image stores it explicitly) and encode to file.
		auto resource = m_ResourceManager->GetTexture(textureHandle).GetD3D12Resource();

		gassert(
			resource != nullptr,
			"ScreenshotRaw failed to get D3D12Resource from texture handle");

		auto resourceDesc = resource->GetDesc();

		const void* mapped = m_ResourceManager->MapReadback(readback);

		DirectX::Image image = {};
		image.width          = static_cast<size_t>(resourceDesc.Width);
		image.height         = static_cast<size_t>(resourceDesc.Height);
		image.format         = resourceDesc.Format;
		image.rowPitch       = static_cast<size_t>(layout.rowPitch);
		image.slicePitch     = static_cast<size_t>(layout.rowPitch) * resourceDesc.Height;
		image.pixels = const_cast<uint8_t*>(static_cast<const uint8_t*>(mapped) + layout.offset);

		std::wstring widePath(filepath.begin(), filepath.end());

		// DDS only for now: it needs no COM/WIC init and stores the exact texture
		// format, which is what the golden-image comparison wants.
		HRESULT hr = DirectX::SaveToDDSFile(image, DirectX::DDS_FLAGS_NONE, widePath.c_str());

		m_ResourceManager->UnmapReadback(readback);
		m_ResourceManager->DestroyReadbackBuffer(readback, fence, false);

		if (FAILED(hr))
		{
			throw GraphicsError(
				std::format(
					"ScreenshotRaw failed to save '{}' (hr=0x{:08X})",
					filepath,
					static_cast<uint32_t>(hr)));
		}
	}

	GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
