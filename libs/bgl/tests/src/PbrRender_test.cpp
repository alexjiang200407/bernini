#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestOptions.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	struct DiagAssertionHandler : public bgl::IGpuAssertionHandler
	{
		int                   calls = 0;
		std::vector<uint32_t> errcodes;

		void
		OnGpuAssertion(const bgl::GpuAssertionReport& report) noexcept override
		{
			++calls;
			errcodes.assign(report.errcodes.begin(), report.errcodes.end());
		}
	};
}

// PBR with Image Based Lighting
TEST_CASE("PBR instances render headlessly", "[pbr][ibl][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;
	opts.logLevel         = bgl::GraphicsOptions::LogLevel::kTrace;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto ctx = gfx->CreateRenderContext();

	DiagAssertionHandler handler;
	ctx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = ctx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	view->SetEnvironmentMap(
		{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

	auto metalMat = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = .6f, .roughnessFactor = .3f });

	auto sphere = scene->AddSphereGeom(32, 32, 5.0f, metalMat);

	auto transform = glm::mat4(1.0f);
	view->CreateStaticMeshInstance(sphere, transform);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i)
	{
		ctx->DrawFrame(target, job);
	}

	ctx->ScreenshotPng(target, "assets/golden/pbr_ibl.got.png");

	CHECK(
		bgl::test::MatchesGolden("assets/golden/pbr_ibl.exp.png", "assets/golden/pbr_ibl.got.png"));

	std::string ecStr;
	for (auto ec : handler.errcodes)
	{
		ecStr += std::to_string(ec) + " ";
	}
	INFO("GPU assertion calls: " << handler.calls << " errcodes: [" << ecStr << "]");
}

// A loose (per-channel) material whose channels are all unrouted resolves to the same defaults as a
// PbrMaterial (white base/ORM, flat normal). With identical factors it must render byte-for-byte the
// same as the PBR case above -- so it validates the loose PSO / buffer / shader against the SAME
// golden. This is the "editor material with trivial routing == triplet material" equivalence check.
TEST_CASE("Loose PBR material renders equivalently to PBR", "[pbr][loose][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;
	opts.logLevel         = bgl::GraphicsOptions::LogLevel::kTrace;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto ctx = gfx->CreateRenderContext();

	DiagAssertionHandler handler;
	ctx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = ctx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxLoosePbrMaterials    = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	view->SetEnvironmentMap(
		{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
	      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

	auto looseDesc            = bgl::LoosePbrMaterialDesc();
	looseDesc.baseColorFactor = glm::vec4(1.0f);
	looseDesc.metallicFactor  = .6f;
	looseDesc.roughnessFactor = .3f;
	auto looseMat             = scene->CreateLoosePbrMaterial(looseDesc);

	auto sphere = scene->AddSphereGeom(32, 32, 5.0f, looseMat);

	auto transform = glm::mat4(1.0f);
	view->CreateStaticMeshInstance(sphere, transform);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i)
	{
		ctx->DrawFrame(target, job);
	}

	ctx->ScreenshotPng(target, "assets/golden/loose_pbr_ibl.got.png");

	// Compares against the SAME golden as the PBR case: loose-with-defaults must match PBR-with-defaults.
	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/pbr_ibl.exp.png",
			"assets/golden/loose_pbr_ibl.got.png"));

	std::string ecStr;
	for (auto ec : handler.errcodes)
	{
		ecStr += std::to_string(ec) + " ";
	}
	INFO("GPU assertion calls: " << handler.calls << " errcodes: [" << ecStr << "]");
}
