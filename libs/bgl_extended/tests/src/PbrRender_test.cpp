#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
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

	DiagAssertionHandler handler;
	gfx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 512;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 800000;
	sceneDesc.initialIndices              = 20000;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

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
		gfx->DrawFrame(target, job);
	}

	gfx->ScreenshotPng(target, "assets/golden/pbr_ibl.got.png");

	CHECK(
		bgl::test::MatchesGolden("assets/golden/pbr_ibl.exp.png", "assets/golden/pbr_ibl.got.png"));

	std::string ecStr;
	for (auto ec : handler.errcodes)
	{
		ecStr += std::to_string(ec) + " ";
	}
	INFO("GPU assertion calls: " << handler.calls << " errcodes: [" << ecStr << "]");
}

/**
 * A mirror far enough away that one pixel covers a wide cone of its reflection.
 *
 * The prefilter chain is sampled at an explicit LOD, so roughness alone would pin a mirror to mip 0
 * at every distance and each pixel would fetch full resolution through a footprint many texels wide
 * -- which speckles as grain rather than resolving anything. PbrShading floors the LOD by the
 * reflection's screen-space derivatives to stop that, and this is the case that exercises it: the
 * near-filling sphere above barely engages the term, because its footprint is under a texel except
 * at the silhouette.
 *
 * Asserted on high-frequency energy rather than against a golden, because what matters is how much
 * grain there is and not one particular set of pixels.
 *
 * Measured on this scene: 0.0097 with no footprint term at all, 0.0024 with the shipped one, and
 * 0.0008 if its cap is lifted. The threshold sits between the first two, so removing the term fails
 * this and tightening the cap does not -- it pins that the correction is working, not that it reaches
 * some absolute smoothness. The cap is deliberate and costs grain; PbrShading says why.
 */
TEST_CASE("A distant mirror does not alias its reflection", "[pbr][ibl][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 4096;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 800000;
	sceneDesc.initialIndices              = 20000;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	// A pure mirror: roughness 0 asks for mip 0, so nothing but the footprint term can save it.
	auto mirror = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = 1.0f, .roughnessFactor = 0.0f });

	auto sphere    = scene->AddSphereGeom(32, 32, 5.0f, mirror);
	auto transform = glm::mat4(1.0f);
	view->CreateStaticMeshInstance(sphere, transform);

	// Far enough back that the sphere is roughly 40 px across in a 400x300 view.
	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 75.0f),
			glm::vec3(0.0f, 0.0f, 74.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

	const std::string shot = "assets/golden/pbr_distant_mirror.got.png";
	gfx->ScreenshotPng(target, shot);

	// The sphere sits at the centre of the frame and spans roughly 38 px, so a 20 px box is the
	// largest that stays inside the silhouette -- its corners must not clip the black background,
	// whose edge would swamp the measurement with contrast that has nothing to do with aliasing.
	const float energy = bgl::test::AliasEnergy(shot, 190, 140, 20, 20);
	INFO("adjacent-pixel energy in the mirror: " << energy);
	CHECK(energy < 0.005f);
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

	DiagAssertionHandler handler;
	gfx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 512;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 800000;
	sceneDesc.initialIndices              = 20000;
	sceneDesc.initialLoosePbrMaterials    = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

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
		gfx->DrawFrame(target, job);
	}

	gfx->ScreenshotPng(target, "assets/golden/loose_pbr_ibl.got.png");

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
