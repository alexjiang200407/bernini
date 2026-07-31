#include "cmd/CommandQueue_metal.h"
#include "device/Device_metal.h"

#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"

#include <core/file/file.h>
#include <core/ref/RefCounter.h>

namespace fs = std::filesystem;

namespace bgl
{
	/**
	 * The Metal façade. Owns the device, the resource manager and the submission queue.
	 *
	 * The frame path is not wired: RenderContext builds every renderer PSO in its constructor, which
	 * needs the meshlet pipelines, so it cannot be constructed until those land. Everything below
	 * GetDevice/GetResourceManagerCpy therefore throws.
	 */
	class Graphics final : public core::RefCounter<GraphicsBase>
	{
	public:
		explicit Graphics(const GraphicsOptions& opts) : m_Opts(opts)
		{
			auto     libraryPath = core::file::get_library_path();
			fs::path logPath     = libraryPath.parent_path() / "bgl.log";

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

			m_Pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			NS::SharedPtr<MTL::Device> mtlDevice =
				NS::TransferPtr(MTL::CreateSystemDefaultDevice());
			if (!mtlDevice)
			{
				core::throw_runtime_error("no Metal device available");
			}

			logger::info("Metal device: {}", mtlDevice->name()->utf8String());

			m_Device = core::SharedRef<Device>::Make(mtlDevice.get());

			auto rmDesc               = ResourceManagerDesc();
			rmDesc.maxCbvSrvUavs      = opts.maxCbvSrvUavs;
			rmDesc.maxRtvs            = opts.maxRtvs;
			rmDesc.maxDsvs            = opts.maxDsvs;
			rmDesc.maxTextures        = opts.maxTextures;
			rmDesc.maxSamplers        = opts.maxSamplers;
			rmDesc.maxReadbackBuffers = opts.maxReadbackBuffers;
			m_ResourceManager         = m_Device->CreateResourceManager(rmDesc);

			m_Context =
				std::make_unique<RenderContext>(m_Device, m_ResourceManager, opts.enableDebugLayer);

			logger::info("BGL initialized successfully.");
		}

		IDevice*
		GetDevice() const noexcept override
		{
			return m_Device.Get();
		}

		core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const noexcept override
		{
			return m_ResourceManager;
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
		static constexpr const char* c_Unimplemented =
			"Metal backend: the frame path is not implemented yet";

		GraphicsOptions                    m_Opts;
		NS::SharedPtr<NS::AutoreleasePool> m_Pool;
		DeviceRef                          m_Device;
		ResourceManagerRef                 m_ResourceManager;

		// Declared last so it is destroyed first: its teardown idles the GPU and releases pass
		// resources through the members above, which must outlive it.
		std::unique_ptr<RenderContext> m_Context;
	};

	BGL_API GraphicsRef
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
