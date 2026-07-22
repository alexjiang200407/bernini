#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t kWidth  = 600;
	constexpr uint32_t kHeight = 800;

	bgl::SceneDesc
	CubeSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 8;
		desc.maxMeshlets             = 512;
		desc.maxSubmeshes            = 8;
		desc.maxVertexBufferByteSize = 800000;
		desc.maxIndices              = 20000;
		return desc;
	}

	// The same cube RenderGeometry's golden was rendered from, so both contexts' output can be
	// compared against the one committed image.
	bgl::RenderJob
	CubeJob(const bgl::SceneViewRef& view)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(kWidth) / static_cast<float>(kHeight),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));
		return job;
	}
}

// The point of IRenderContext: a second context has its own frame state, so frames on two
// contexts can be open at the same time (per-IGraphics this throws "frame already active"), each
// presenting to its own target, and both produce correct pixels. Everything here goes through the
// public API alone.
TEST_CASE("Two contexts render interleaved frames independently", "[rendercontext][multicontext]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	auto gfx                      = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto ctx = gfx->CreateRenderContext();
	REQUIRE(ctx != nullptr);

	// A target is bound to the context that created it.
	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(kWidth);
	targetDesc.height   = static_cast<int>(kHeight);
	targetDesc.headless = true;

	auto primaryTarget = gfx->CreateRenderTarget(targetDesc);
	auto ctxTarget     = ctx->CreateRenderTarget(targetDesc);
	REQUIRE(primaryTarget != nullptr);
	REQUIRE(ctxTarget != nullptr);

	// One scene per context: the S4 interim constraint is that contexts do not share a Scene.
	auto sceneA = gfx->CreateScene(CubeSceneDesc());
	auto sceneB = gfx->CreateScene(CubeSceneDesc());
	auto viewA  = gfx->CreateSceneView(sceneA, 8);
	auto viewB  = gfx->CreateSceneView(sceneB, 8);

	viewA->CreateStaticMeshInstance(sceneA->AddCubeGeom(), glm::mat4(1.0f));
	viewB->CreateStaticMeshInstance(sceneB->AddCubeGeom(), glm::mat4(1.0f));

	// Both frames are open at once, and neither BeginFrame throws: frame-active state is
	// per-context. Submission order is interleaved on purpose.
	gfx->BeginFrame(primaryTarget);
	ctx->BeginFrame(ctxTarget);

	gfx->Draw(CubeJob(viewA));
	ctx->Draw(CubeJob(viewB));

	ctx->EndFrame();
	gfx->EndFrame();

	gfx->ScreenshotPng(primaryTarget, "assets/golden/multictx_primary.got.png");
	ctx->ScreenshotPng(ctxTarget, "assets/golden/multictx_second.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/multictx_primary.got.png"));
	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/multictx_second.got.png"));
}
