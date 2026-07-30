#include "RenderTarget_metal.h"

#include "cmd/CommandQueue_metal.h"
#include "device/Device_metal.h"

#include "gfx/GraphicsBase.h"
#include "resource/ResourceManager.h"

#include <bgl/RenderContext.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	namespace
	{
		// A hardcoded RGB triangle in clip space -- no vertex buffer, positions come from vertex_id.
		// This is the skeleton's "draw": it proves device -> layer -> pipeline -> drawable -> present.
		constexpr const char* c_TriangleMsl = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float3 col; };
vertex VOut vmain(uint vid [[vertex_id]]) {
    const float2 p[3] = { float2(0.0, 0.6), float2(-0.6, -0.6), float2(0.6, -0.6) };
    const float3 c[3] = { float3(1,0,0), float3(0,1,0), float3(0,0,1) };
    VOut o;
    o.pos = float4(p[vid], 0.0, 1.0);
    o.col = c[vid];
    return o;
}
fragment float4 fmain(VOut in [[stage_in]]) { return float4(in.col, 1.0); }
)";

		NS::String*
		Str(const char* utf8)
		{
			return NS::String::string(utf8, NS::UTF8StringEncoding);
		}
	}

	class Graphics final : public core::RefCounter<GraphicsBase>
	{
	public:
		explicit Graphics(const GraphicsOptions& opts)
		{
			logger::set_level(static_cast<logger::level::level_enum>(opts.logLevel));

			NS::SharedPtr<NS::AutoreleasePool> pool =
				NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			NS::SharedPtr<MTL::Device> mtlDevice =
				NS::TransferPtr(MTL::CreateSystemDefaultDevice());
			if (!mtlDevice)
			{
				core::throw_runtime_error("no Metal device available");
			}

			m_Device          = core::SharedRef<Device>::Make(mtlDevice.get());
			m_Queue           = m_Device->CreateGraphicsCommandQueue();
			m_MtlDevice       = mtlDevice.get();
			m_MtlQueue        = static_cast<CommandQueue*>(m_Queue.Get())->GetMTLCommandQueue();
			m_ResourceManager = m_Device->CreateResourceManager(ResourceManagerDesc{}, m_Queue);

			logger::info("Metal device: {}", m_MtlDevice->name()->utf8String());

			BuildTrianglePipeline();
		}

		~Graphics() noexcept override = default;

		RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc) override
		{
			return core::SharedRef<RenderTarget>::Make(desc, m_MtlDevice);
		}

		void
		BeginFrame(const RenderTargetRef& target) override
		{
			if (m_FrameActive)
			{
				core::throw_runtime_error("BeginFrame called while a frame is already active");
			}
			m_FrameActive = true;
			m_FramePool   = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

			auto* rt   = static_cast<RenderTarget*>(target.Get());
			m_Drawable = rt->Layer()->nextDrawable();
			if (m_Drawable == nullptr)
			{
				// A transient miss (occluded window, mid-resize, pool exhausted). The frame stays
				// active so the paired EndFrame is still valid; it no-ops when the drawable is null.
				return;
			}

			MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
			MTL::RenderPassColorAttachmentDescriptor* c = pass->colorAttachments()->object(0);
			c->setTexture(m_Drawable->texture());
			c->setLoadAction(MTL::LoadActionClear);
			c->setStoreAction(MTL::StoreActionStore);
			c->setClearColor(MTL::ClearColor::Make(0.09, 0.09, 0.11, 1.0));

			m_Cmd                          = m_MtlQueue->commandBuffer();
			MTL::RenderCommandEncoder* enc = m_Cmd->renderCommandEncoder(pass);
			enc->setRenderPipelineState(m_TrianglePipeline.get());
			enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
			enc->endEncoding();
		}

		void
		Draw(const RenderContext&) override
		{
			// Scene rendering is not wired up yet; the skeleton draws its triangle in BeginFrame.
		}

		void
		EndFrame() override
		{
			if (!m_FrameActive)
			{
				core::throw_runtime_error("EndFrame called with no active frame");
			}
			if (m_Drawable != nullptr)
			{
				m_Cmd->presentDrawable(m_Drawable);
				m_Cmd->commit();
			}
			m_Cmd      = nullptr;
			m_Drawable = nullptr;
			m_FramePool.reset();
			m_FrameActive = false;
		}

		void
		Resize(const RenderTargetRef& target, uint32_t width, uint32_t height) override
		{
			if (m_FrameActive)
			{
				core::throw_runtime_error("Resize called between BeginFrame and EndFrame");
			}
			static_cast<RenderTarget*>(target.Get())->Resize(width, height);
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

		SceneRef
		CreateScene(SceneDesc) override
		{
			core::throw_runtime_error("Metal backend: CreateScene not implemented yet");
		}

		SceneViewRef
		CreateSceneView(const SceneRef&, uint32_t) override
		{
			core::throw_runtime_error("Metal backend: CreateSceneView not implemented yet");
		}

		void
		ScreenshotPng(const RenderTargetRef&, const std::string&) override
		{
			core::throw_runtime_error("Metal backend: ScreenshotPng not implemented yet");
		}

		assetlib::ImageData
		ScreenshotToMemory(const RenderTargetRef&) override
		{
			core::throw_runtime_error("Metal backend: ScreenshotToMemory not implemented yet");
		}

		void
		SetGpuAssertionHandler(IGpuAssertionHandler*) noexcept override
		{}

		void
		DiscardPendingGpuAssertions() noexcept override
		{}

	private:
		void
		BuildTrianglePipeline()
		{
			NS::Error*                  error = nullptr;
			NS::SharedPtr<MTL::Library> lib =
				NS::TransferPtr(m_MtlDevice->newLibrary(Str(c_TriangleMsl), nullptr, &error));
			if (!lib)
			{
				core::throw_runtime_error(
					"triangle shader failed to compile: {}",
					error->localizedDescription()->utf8String());
			}

			NS::SharedPtr<MTL::Function> vfn = NS::TransferPtr(lib->newFunction(Str("vmain")));
			NS::SharedPtr<MTL::Function> ffn = NS::TransferPtr(lib->newFunction(Str("fmain")));

			NS::SharedPtr<MTL::RenderPipelineDescriptor> desc =
				NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
			desc->setVertexFunction(vfn.get());
			desc->setFragmentFunction(ffn.get());
			desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

			m_TrianglePipeline =
				NS::TransferPtr(m_MtlDevice->newRenderPipelineState(desc.get(), &error));
			if (!m_TrianglePipeline)
			{
				core::throw_runtime_error(
					"triangle pipeline failed: {}",
					error->localizedDescription()->utf8String());
			}
		}

		DeviceRef                               m_Device;
		CommandQueueRef                         m_Queue;
		ResourceManagerRef                      m_ResourceManager;
		MTL::Device*                            m_MtlDevice = nullptr;  // owned by m_Device
		MTL::CommandQueue*                      m_MtlQueue  = nullptr;  // owned by m_Queue
		NS::SharedPtr<MTL::RenderPipelineState> m_TrianglePipeline;

		bool                               m_FrameActive = false;
		NS::SharedPtr<NS::AutoreleasePool> m_FramePool;
		MTL::CommandBuffer*                m_Cmd      = nullptr;  // autoreleased into m_FramePool
		CA::MetalDrawable*                 m_Drawable = nullptr;  // autoreleased into m_FramePool
	};

	BGL_API GraphicsRef
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
