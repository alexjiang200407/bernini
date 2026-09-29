#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "device/Device_d3d12.h"
#include "gfx/DrawBucketTable.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderContext.h"
#include "gfx/surface_registry.h"
#include "overlay/Overlay.h"
#include "resource/ResourceManager_d3d12.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include <bgl/PassTiming.h>
#include <bgpu/GpuContext.h>
#include <core/log/log.h>
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <vector>

namespace fs = std::filesystem;

namespace bgl
{

	class IScene;

	class Graphics : public core::RefCounter<GraphicsBase>
	{
	public:
		Graphics(bgpu::GpuContextRef context, const GraphicsOptions& opts);
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

		const RenderContext*
		GetRenderContext() const noexcept override
		{
			return m_Context.get();
		}

		void
		WaitIdle() noexcept override
		{
			m_Context->WaitIdle();
		}

		std::span<const SurfaceType>
		GetSurfaceTypes() const noexcept override
		{
			return m_SurfaceTypes;
		}

		SceneRef
		CreateScene(SceneDesc desc) override
		{
			return core::SharedRef<Scene>::Make(std::move(desc), m_ResourceManager, m_SurfaceTypes);
		}

		SceneViewRef
		CreateSceneView(const SceneRef& scene, uint32_t initialInstances) override
		{
			return core::SharedRef<SceneView>::Make(
				scene,
				initialInstances,
				m_ResourceManager,
				m_DrawBucketTable);
		}

		OverlayRef
		CreateOverlay() override
		{
			return core::SharedRef<Overlay>::Make(m_ResourceManager);
		}

		void
		DrawOverlay(const OverlayJob& job) override
		{
			m_Context->DrawOverlay(job);
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

		PassTimings
		GetPassTimings(const RenderTargetRef& target) override
		{
			return m_Context->GetPassTimings(target);
		}

	private:
		GraphicsOptions m_Opts;

		DeviceRef m_Device;

		ResourceManagerRef m_ResourceManager;

		std::shared_ptr<DrawBucketTable> m_DrawBucketTable;

		// Declared last so it is destroyed first: its teardown idles the GPU and releases pass and
		// debug resources through the members above, which must outlive it.
		std::unique_ptr<RenderContext> m_Context;

		// Fixed at construction, before the pipelines that draw them were built.
		std::vector<SurfaceType> m_SurfaceTypes;
	};
}

namespace bgl
{
	Graphics::Graphics(bgpu::GpuContextRef context, const GraphicsOptions& opts) : m_Opts(opts)
	{
		auto device = core::SharedRef<Device>::Make(std::move(context));
		m_Device    = device;

		{
			auto resourceManagerDesc               = ResourceManagerDesc();
			resourceManagerDesc.maxCbvSrvUavs      = m_Opts.maxCbvSrvUavs;
			resourceManagerDesc.maxBuffers         = m_Opts.maxBuffers;
			resourceManagerDesc.maxSrvs            = m_Opts.maxSrvs;
			resourceManagerDesc.maxDsvs            = m_Opts.maxDsvs;
			resourceManagerDesc.maxRtvs            = m_Opts.maxRtvs;
			resourceManagerDesc.maxTextures        = m_Opts.maxTextures;
			resourceManagerDesc.maxSamplers        = m_Opts.maxSamplers;
			resourceManagerDesc.maxBufferSrvs      = m_Opts.maxBufferSrvs;
			resourceManagerDesc.maxReadbackBuffers = m_Opts.maxReadbackBuffers;

			m_ResourceManager = m_Device->CreateResourceManager(resourceManagerDesc);
		}

		// Before the context: it builds every pipeline, and a slot's pipelines compile against
		// whatever module this bound to that slot.
		m_SurfaceTypes =
			RegisterSurfaces(*m_Device, device->GetGpuContext().GetDesc().clientShaderDir);

		m_DrawBucketTable = std::make_shared<DrawBucketTable>();
		m_Context         = std::make_unique<RenderContext>(
			m_Device,
			m_ResourceManager,
			m_DrawBucketTable,
			m_SurfaceTypes,
			device->GetGpuContext().GetDesc().enableDebugLayer);

		// The always-on set is built by the RenderContext above; the per-bucket kernels are built by
		// the first Draw that demands each, and that path drops the sessions again after every
		// batch. This release covers the start-up build.
		device->ReleaseSlangSession();
	}

	Graphics::~Graphics() noexcept
	{
		spdlog::trace("~Graphics");

		m_Context.reset();
		m_ResourceManager.Reset();
		m_Device.Reset();
	}

	GraphicsRef
	CreateGraphics(core::SharedRef<bgpu::GpuContext> context, const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(std::move(context), opts);
	}
}
