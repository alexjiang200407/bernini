#include "MetalErrorChecker.h"
#include "cmd/CommandQueue_metal.h"
#include "device/Device_metal.h"

#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"

#include <core/file/file.h>
#include <core/log/log.h>
#include <core/ref/RefCounter.h>

namespace fs = std::filesystem;

namespace bgl
{
	namespace
	{
		/**
		 * Writes one frame to a .gputrace bundle, which Xcode's Metal debugger opens. Capturing
		 * needs no Xcode; only reading the result does, so a machine with Command Line Tools alone
		 * can still produce the trace.
		 */
		class FrameCapture
		{
		public:
			// @pre MTL_CAPTURE_ENABLED=1 was set before the process created its device; Metal
			//      otherwise refuses, and this throws rather than leave the caller with no trace and
			//      no reason.
			void
			Begin(MTL::Device* device, const std::string& path)
			{
				MTL::CaptureManager* manager = MTL::CaptureManager::sharedCaptureManager();
				if (!manager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument))
				{
					// Metal reports the destination as unsupported rather than naming the reason,
					// and by far the usual one is the environment, not the device.
					core::throw_runtime_error(
						"gpuCapturePath is set but Metal will not write a .gputrace. Set "
						"MTL_CAPTURE_ENABLED=1 in the environment before running.");
				}

				std::error_code ec;
				fs::remove_all(path, ec);

				NS::SharedPtr<MTL::CaptureDescriptor> desc =
					NS::TransferPtr(MTL::CaptureDescriptor::alloc()->init());
				desc->setCaptureObject(device);
				desc->setDestination(MTL::CaptureDestinationGPUTraceDocument);
				desc->setOutputURL(
					NS::URL::fileURLWithPath(
						NS::String::string(path.c_str(), NS::UTF8StringEncoding)));

				NS::Error* error = nullptr;
				if (!manager->startCapture(desc.get(), &error))
				{
					core::throw_runtime_error(
						"Metal frame capture failed to start: {}",
						GetErrorDescription(error));
				}
				m_Active = true;
			}

			void
			End(const std::string& path) noexcept
			{
				if (!m_Active)
					return;
				MTL::CaptureManager::sharedCaptureManager()->stopCapture();
				m_Active = false;
				m_Done   = true;
				logger::info("Metal frame capture written to {}", path);
			}

			[[nodiscard]] bool
			Wanted() const noexcept
			{
				return !m_Done && !m_Active;
			}

			[[nodiscard]] bool
			Active() const noexcept
			{
				return m_Active;
			}

		private:
			bool m_Active = false;
			bool m_Done   = false;
		};
	}

	/**
	 * The Metal façade. Owns the device, the resource manager and the submission queue, and forwards
	 * the frame path to a RenderContext.
	 */
	class Graphics final : public core::RefCounter<GraphicsBase>
	{
	public:
		explicit Graphics(const GraphicsOptions& opts) : m_Opts(opts)
		{
			core::logging::init_file_logger("bgl.log", static_cast<int>(opts.logLevel));

			m_Pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			NS::SharedPtr<MTL::Device> mtlDevice =
				NS::TransferPtr(MTL::CreateSystemDefaultDevice());
			if (!mtlDevice)
			{
				core::throw_runtime_error("no Metal device available");
			}

			logger::info("Metal device: {}", mtlDevice->name()->utf8String());

			// Metal's validation is switched on by the environment, not by us, so the option alone
			// cannot say whether it is running. Either variable instruments shaders enough that a
			// binary archive written without them no longer describes what the driver will run.
			const bool gpuValidation = opts.enableGPUValidationLayer ||
			                           std::getenv("MTL_SHADER_VALIDATION") != nullptr ||
			                           std::getenv("METAL_DEVICE_WRAPPER_TYPE") != nullptr;

			core::SharedRef<Device> device =
				core::SharedRef<Device>::Make(mtlDevice.get(), opts.shaderCacheDir, !gpuValidation);
			m_Device = device;

			auto rmDesc               = ResourceManagerDesc();
			rmDesc.maxCbvSrvUavs      = opts.maxCbvSrvUavs;
			rmDesc.maxBuffers         = opts.maxBuffers;
			rmDesc.maxSrvs            = opts.maxSrvs;
			rmDesc.maxRtvs            = opts.maxRtvs;
			rmDesc.maxDsvs            = opts.maxDsvs;
			rmDesc.maxTextures        = opts.maxTextures;
			rmDesc.maxSamplers        = opts.maxSamplers;
			rmDesc.maxReadbackBuffers = opts.maxReadbackBuffers;
			m_ResourceManager         = m_Device->CreateResourceManager(rmDesc);

			m_Context =
				std::make_unique<RenderContext>(m_Device, m_ResourceManager, opts.enableDebugLayer);

			// Every PSO the renderer will ever use is built by the RenderContext above, so nothing
			// past this point compiles a shader and the Slang core module can stop occupying a few
			// hundred megabytes. A later CreatePipeline would silently recreate the session.
			device->ReleaseSlangSession();

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
			if (!m_Opts.gpuCapturePath.empty() && m_Capture.Wanted())
				m_Capture.Begin(m_Device->As<Device>()->GetMTLDevice(), m_Opts.gpuCapturePath);

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

			// After EndFrame, so the trace holds a submitted frame rather than a half-recorded one.
			if (m_Capture.Active())
				m_Capture.End(m_Opts.gpuCapturePath);
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
		GraphicsOptions                    m_Opts;
		FrameCapture                       m_Capture;
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
