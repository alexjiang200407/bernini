// bgl_extended_tests globs every .cpp under tests/ whatever the backend, so a Metal-only case has to
// exclude itself: a CAMetalLayer is the Metal backend's window.
#if defined(RENDERER_BACKEND_METAL)

#	include "util/GpuValidation.h"
#	include "util/TestGraphics.h"
#	include "util/TestOptions.h"
#	include <QuartzCore/QuartzCore.hpp>
#	include <bgl/IGraphics.h>
#	include <bgl/IRenderTarget.h>
#	include <catch2/catch_test_macros.hpp>

// Every other case renders headless, so this is the one that reaches the present path: the ring's
// backbuffer blitted into the layer's drawable on the target's own command list, frame after frame.
// A layer on no window still hands out drawables, which is all the present needs.
TEST_CASE("A windowed target presents into its layer", "[present][graphics]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
	NS::SharedPtr<CA::MetalLayer>      layer = NS::RetainPtr(CA::MetalLayer::layer());
	REQUIRE(layer.get() != nullptr);

	{
		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto desc     = bgl::RenderTargetDesc();
		desc.width    = 64;
		desc.height   = 32;
		desc.headless = false;
		desc.wnd      = layer.get();

		auto target = gfx->CreateRenderTarget(desc);
		REQUIRE(target != nullptr);
		CHECK(layer->pixelFormat() == MTL::PixelFormatBGRA8Unorm_sRGB);

		for (int frame = 0; frame < 4; ++frame)
		{
			gfx->BeginFrame(target);
			gfx->EndFrame();
		}
		gfx->WaitIdle();
	}
}

#endif
