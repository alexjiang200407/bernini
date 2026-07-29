#include "device/Device_wgpu.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "resource/ResourceManager_wgpu.h"
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
		GraphicsOptions m_Opts;

		DeviceRef m_Device;

		ResourceManagerRef m_ResourceManager;

		// Declared last so it is destroyed first: its teardown idles the GPU and releases pass
		// resources through the members above, which must outlive it.
		std::unique_ptr<RenderContext> m_Context;
	};
}

namespace bgl
{
	Graphics::Graphics(const GraphicsOptions& opts) : m_Opts(opts)
	{
		{
			auto     libraryPath = core::file::get_library_path();
			fs::path logPath     = libraryPath.parent_path() / "bgl.log";

			// Truncate once per process so a single run accumulates every device's
			// messages instead of each new Graphics clobbering the previous log.
			static bool g_LogTruncated = false;
			const bool  truncate       = !g_LogTruncated;
			g_LogTruncated             = true;

			auto sink =
				std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), truncate);

			auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

			log->set_level(static_cast<logger::level::level_enum>(opts.logLevel));
			log->flush_on(static_cast<logger::level::level_enum>(opts.logLevel));

			spdlog::set_default_logger(std::move(log));
			spdlog::set_pattern("[%H:%M:%S:%e] [thread %t] [%l] %v"s);

			logger::info("BGL initialized successfully (WebGPU).");
		}

		// The D3D12 debug-layer, PIX and GPU-validation options have no WebGPU counterpart here;
		// Dawn's own validation is always on and its messages already land in the log through the
		// device's error callbacks. The shader cache is not wired to this backend yet either.
		if (!m_Opts.shaderCacheDir.empty())
		{
			logger::info("Shader cache is not implemented on the WebGPU backend; compiling live.");
		}

		m_Device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

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

		// Unlike D3D12 there is no ReleaseSlangSession after this: the wgpu device keeps its
		// session for its lifetime, since nothing caches compiled programs on this backend yet.
		m_Context =
			std::make_unique<RenderContext>(m_Device, m_ResourceManager, m_Opts.enableDebugLayer);
	}

	Graphics::~Graphics() noexcept
	{
		logger::trace("~Graphics");

		m_Context.reset();
		m_ResourceManager.Reset();
		m_Device.Reset();
	}

	GraphicsRef
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
