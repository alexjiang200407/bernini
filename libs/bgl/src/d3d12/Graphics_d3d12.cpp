#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "resource/ResourceManager_d3d12.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"

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

		SceneRef
		CreateScene(SceneDesc desc) override
		{
			return core::SharedRef<Scene>::Make(std::move(desc), m_ResourceManager);
		}

		SceneViewRef
		CreateSceneView(const SceneRef& scene, uint32_t maxInstances) override
		{
			return core::SharedRef<SceneView>::Make(scene, maxInstances, m_ResourceManager);
		}

		RenderContextRef
		CreateRenderContext() override
		{
			return core::SharedRef<RenderContext>::Make(
				m_Device,
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

		DeviceRef m_Device;

		wrl::ComPtr<ID3D12Debug1>     m_DebugController;
		wrl::ComPtr<IDXGIInfoQueue>   m_DxgiInfoQueue;
		wrl::ComPtr<ID3D12InfoQueue1> m_D3D12InfoQueue;
		DWORD                         m_MessageCallbackCookie = 0;

		ResourceManagerRef m_ResourceManager;
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

		m_Device = core::SharedRef<Device>::Make(
			m_D3D12Device,
			m_SlangGlobalSession,
			m_Opts.shaderCacheDir,
			m_Opts.enableGPUValidationLayer);

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
			resourceManagerDesc.maxDsvs            = m_Opts.maxDsvs;
			resourceManagerDesc.maxRtvs            = m_Opts.maxRtvs;
			resourceManagerDesc.maxTextures        = m_Opts.maxTextures;
			resourceManagerDesc.maxSamplers        = m_Opts.maxSamplers;
			resourceManagerDesc.maxReadbackBuffers = m_Opts.maxReadbackBuffers;

			m_ResourceManager = m_Device->CreateResourceManager(resourceManagerDesc);
		}
	}

	Graphics::~Graphics() noexcept
	{
		logger::trace("~Graphics");

		m_ResourceManager.Reset();
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
