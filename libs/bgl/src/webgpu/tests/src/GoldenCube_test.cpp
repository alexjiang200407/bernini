#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

// The W3 gate: the public API end to end on the WebGPU backend, compared against the same committed
// golden the D3D12 suite renders. CreateGraphics builds every pass, the frame runs the full
// cull -> compact -> expand -> vertex-pulling chain, and the pixels have to land where D3D12's mesh
// shaders put them -- geometry, culling and rasterization are image-invariant across the two paths.
//
// The tolerance is per-backend (the plan's Metal precedent): rasterizers differ in tie-breaking and
// derivative behaviour at silhouettes, so cross-backend matches are close, not bit-equal.
TEST_CASE("The cube golden matches through the WebGPU public API", "[wgpu][golden]")
{
	constexpr uint32_t c_Width  = 600;
	constexpr uint32_t c_Height = 800;

	auto opts     = bgl::GraphicsOptions();
	opts.logLevel = bgl::GraphicsOptions::LogLevel::kInfo;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 512;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 800000;
	sceneDesc.initialIndices              = 20000;

	auto scene = gfxBase->CreateScene(sceneDesc);
	auto view  = gfxBase->CreateSceneView(scene, 8);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			500.0f);

	auto cubeGeom = scene->AddCubeGeom();
	view->CreateStaticMeshInstance(cubeGeom, glm::mat4(1.0f));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, "assets/golden/cube.wgpu.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/cube.exp.png",
			"assets/golden/cube.wgpu.got.png",
			5e-4f));
}
