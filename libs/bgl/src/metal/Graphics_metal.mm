#include "RenderTarget_metal.h"

#include "gfx/GraphicsBase.h"

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
	}

	class Graphics final : public core::RefCounter<GraphicsBase>
	{
	public:
		explicit Graphics(const GraphicsOptions& opts) : m_Opts(opts)
		{
			m_Device = MTLCreateSystemDefaultDevice();
			if (m_Device == nil)
			{
				core::throw_runtime_error("no Metal device available");
			}
			m_Queue = [m_Device newCommandQueue];

			logger::info("Metal device: {}", [m_Device.name UTF8String]);

			BuildTrianglePipeline();
		}

		~Graphics() noexcept override = default;

		RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc) override
		{
			return core::SharedRef<RenderTarget>::Make(desc, m_Device);
		}

		void
		BeginFrame(const RenderTargetRef& target) override
		{
			if (m_FrameActive)
			{
				core::throw_runtime_error("BeginFrame called while a frame is already active");
			}
			m_FrameActive = true;

			auto* rt   = static_cast<RenderTarget*>(target.Get());
			m_Drawable = [rt->Layer() nextDrawable];
			if (m_Drawable == nil)
			{
				m_FrameActive = false;
				return;
			}

			MTLRenderPassDescriptor* pass  = [MTLRenderPassDescriptor renderPassDescriptor];
			pass.colorAttachments[0].texture     = m_Drawable.texture;
			pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.09, 0.09, 0.11, 1.0);

			m_Cmd                            = [m_Queue commandBuffer];
			id<MTLRenderCommandEncoder> enc  = [m_Cmd renderCommandEncoderWithDescriptor:pass];
			[enc setRenderPipelineState:m_TrianglePipeline];
			[enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
			[enc endEncoding];
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
			if (m_Drawable != nil)
			{
				[m_Cmd presentDrawable:m_Drawable];
				[m_Cmd commit];
			}
			m_Cmd         = nil;
			m_Drawable    = nil;
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
			return nullptr;
		}

		core::SharedRef<IResourceManager>
		GetResourceManagerCpy() const noexcept override
		{
			return nullptr;
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
			NSError*       error = nil;
			id<MTLLibrary> lib =
				[m_Device newLibraryWithSource:@(c_TriangleMsl) options:nil error:&error];
			if (lib == nil)
			{
				core::throw_runtime_error(
					"triangle shader failed to compile: {}",
					[error.localizedDescription UTF8String]);
			}

			MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
			desc.vertexFunction               = [lib newFunctionWithName:@"vmain"];
			desc.fragmentFunction             = [lib newFunctionWithName:@"fmain"];
			desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

			m_TrianglePipeline =
				[m_Device newRenderPipelineStateWithDescriptor:desc error:&error];
			if (m_TrianglePipeline == nil)
			{
				core::throw_runtime_error(
					"triangle pipeline failed: {}",
					[error.localizedDescription UTF8String]);
			}
		}

		GraphicsOptions              m_Opts;
		id<MTLDevice>                m_Device           = nil;
		id<MTLCommandQueue>          m_Queue            = nil;
		id<MTLRenderPipelineState>   m_TrianglePipeline = nil;

		bool                    m_FrameActive = false;
		id<MTLCommandBuffer>    m_Cmd         = nil;
		id<CAMetalDrawable>     m_Drawable    = nil;
	};

	BGL_API GraphicsRef
	CreateGraphics(const GraphicsOptions& opts)
	{
		return core::SharedRef<Graphics>::Make(opts);
	}
}
