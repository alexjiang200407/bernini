#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "resource/ResourceManager_d3d12.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <core/log/log.h>

namespace fs = std::filesystem;

namespace bgl
{

	class IScene;

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

		void
		WaitIdle() noexcept override
		{
			m_Context->WaitIdle();
		}

		SceneRef
		CreateScene(SceneDesc desc) override
		{
			return core::SharedRef<Scene>::Make(std::move(desc), m_ResourceManager);
		}

		SceneViewRef
		CreateSceneView(const SceneRef& scene, uint32_t initialInstances) override
		{
			return core::SharedRef<SceneView>::Make(scene, initialInstances, m_ResourceManager);
		}

		RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc) override
		{
			return m_Context->CreateRenderTarget(desc);
		}

		void
		BeginFrame(const RenderTargetRef& target) override
		{
			m_Context->BeginFrame(target);
		}

		void
		Draw(const RenderJob& job) override
		{
			m_Context->Draw(job);
		}

		void
		EndFrame() override
		{
			m_Context->EndFrame();
		}

		void
		Resize(const RenderTargetRef& target, uint32_t width, uint32_t height) override
		{
			m_Context->Resize(target, width, height);
		}

		void
		SetRenderScale(const RenderTargetRef& target, float scale) override
		{
			m_Context->SetRenderScale(target, scale);
		}

		void
		ScreenshotPng(const RenderTargetRef& target, const std::string& filepath) override
		{
			m_Context->ScreenshotPng(target, filepath);
		}

		assetlib::ImageData
		ScreenshotToMemory(const RenderTargetRef& target) override
		{
			return m_Context->ScreenshotToMemory(target);
		}

		CaptureTicket
		SubmitCapture(const RenderTargetRef& target) override
		{
			return m_Context->SubmitCapture(target);
		}

		std::optional<assetlib::ImageData>
		TryResolveCapture(CaptureTicket ticket) override
		{
			return m_Context->TryResolveCapture(ticket);
		}

		void
		DiscardCapture(CaptureTicket ticket) noexcept override
		{
			m_Context->DiscardCapture(ticket);
		}

		void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept override
		{
			m_Context->SetGpuAssertionHandler(handler);
		}

		void
		DiscardPendingGpuAssertions() noexcept override
		{
			m_Context->DiscardPendingGpuAssertions();
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
		GraphicsOptions m_Opts;

		DeviceRef m_Device;

		wrl::ComPtr<ID3D12Debug1>     m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue>   m_DxgiInfoQueue;
		wrl::ComPtr<ID3D12InfoQueue1> m_D3D12InfoQueue;
		DWORD                         m_MessageCallbackCookie = 0;

		ResourceManagerRef m_ResourceManager;

		// Declared last so it is destroyed first: its teardown idles the GPU and releases pass and
		// debug resources through the members above, which must outlive it.
		std::unique_ptr<RenderContext> m_Context;
	};
}

namespace bgl
{
	Graphics::Graphics(const GraphicsOptions& opts) : m_Opts(opts)
	{
		{
			core::logging::init_file_logger("bgl.log", static_cast<int>(opts.logLevel));

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

		auto device = core::SharedRef<Device>::Make(
			m_D3D12Device,
			m_Opts.shaderCacheDir,
			m_Opts.enableGPUValidationLayer);
		m_Device = device;

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

		{
			auto resourceManagerDesc               = ResourceManagerDesc();
			resourceManagerDesc.maxCbvSrvUavs      = m_Opts.maxCbvSrvUavs;
			resourceManagerDesc.maxBuffers         = m_Opts.maxBuffers;
			resourceManagerDesc.maxSrvs            = m_Opts.maxSrvs;
			resourceManagerDesc.maxDsvs            = m_Opts.maxDsvs;
			resourceManagerDesc.maxRtvs            = m_Opts.maxRtvs;
			resourceManagerDesc.maxTextures        = m_Opts.maxTextures;
			resourceManagerDesc.maxSamplers        = m_Opts.maxSamplers;
			resourceManagerDesc.maxReadbackBuffers = m_Opts.maxReadbackBuffers;

			m_ResourceManager = m_Device->CreateResourceManager(resourceManagerDesc);
		}

		m_Context = std::make_unique<RenderContext>(
			m_Device,
			m_ResourceManager,
			m_Opts.enableDebugLayer,
			m_Opts.onPipelineProgress);

		// Every PSO the renderer will ever use is built by the RenderContext above, so nothing
		// past this point compiles a shader and the Slang core module can stop occupying a few
		// hundred megabytes. A later CreatePipeline would silently recreate the session.
		device->ReleaseSlangSession();
	}

	Graphics::~Graphics() noexcept
	{
		logger::trace("~Graphics");

		m_Context.reset();
		m_ResourceManager.Reset();
		m_Device.Reset();

		m_DxgiInfoQueue.Reset();
		m_DebugController.Reset();

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

	GraphicsRef
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
