#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	constexpr uint32_t c_Width  = 600;
	constexpr uint32_t c_Height = 800;

	// A translucent plane at world-space depth z, facing the camera. `occlude` selects the
	// depth-pre-pass self-occlusion path.
	void
	addPane(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		const glm::vec4&         baseColor,
		float                    z,
		bool                     occlude = false)
	{
		auto desc = bgl::PbrMaterialDesc();
		desc.baseColorFactor =
			baseColor;  // alpha < 1 drives the blend; no texture needed (white default)
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.9f;
		desc.layerType       = bgl::LayerType::kBlend;
		desc.occlude         = occlude;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)));
	}

	// The same blend material authored the other way: a loose material routes its channels
	// explicitly, and an unrouted one falls back to exactly the PbrMaterial defaults (white for base
	// colour / ORM, flat normal for XY). So with no routes and matching factors the two must shade
	// identically -- while taking opposite branches of the shared transparent pixel shader.
	void
	addLoosePane(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		const glm::vec4&         baseColor,
		float                    z,
		bool                     occlude = false)
	{
		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = baseColor;
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.9f;
		desc.layerType       = bgl::LayerType::kBlend;
		desc.occlude         = occlude;

		auto material = scene->CreateLoosePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)));
	}
}

// Two translucent panes overlap at screen centre: red near the camera, blue behind it. Correct
// back-to-front compositing makes red dominate the overlap; drawing them in PSO/creation order
// instead would let the far blue win. The second half proves the result is a pure function of the
// camera, not of the order the instances were created in -- the whole point of the depth sort.
TEST_CASE(
	"Translucent panes composite back-to-front regardless of creation order",
	"[transparent][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);

	const glm::vec4 red{ 1.0f, 0.03f, 0.03f, 0.5f };
	const glm::vec4 blue{ 0.03f, 0.03f, 1.0f, 0.5f };

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

	// z=5 is nearer the camera (at z=20) than z=0, so red must win the overlap.
	constexpr float c_NearZ = 5.0f;
	constexpr float c_FarZ  = 0.0f;

	const std::string gotFar  = "assets/golden/transparent_far_first.got.png";
	const std::string gotNear = "assets/golden/transparent_near_first.got.png";

	const auto render = [&](bool farFirst, const std::string& path) {
		auto view = gfx->CreateSceneView(scene, 8);
		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

		if (farFirst)
		{
			addPane(scene, view, blue, c_FarZ);
			addPane(scene, view, red, c_NearZ);
		}
		else
		{
			addPane(scene, view, red, c_NearZ);
			addPane(scene, view, blue, c_FarZ);
		}

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, path);
	};

	render(true, gotFar);
	render(false, gotNear);

	// The overlap sits at screen centre. A real hit is well above the black background's ~0 luma.
	const bgl::test::Rgba centre = bgl::test::MeanColor(gotFar, 200, 300, 200, 200);
	INFO("centre rgba = " << centre.r << ", " << centre.g << ", " << centre.b);
	REQUIRE(centre.Luma() > 0.02f);

	// Back-to-front: the near red pane composites over the far blue one.
	CHECK(centre.r > centre.b);

	// Creation order must not matter: far-first and near-first produce the same frame.
	CHECK(bgl::test::MatchesGolden(gotFar, gotNear));
}

// The depth pre-pass makes a self-occluding blend material hide its own back layers: the near red
// pane's depth is stamped first, so the far blue pane is rejected in the overlap instead of bleeding
// through. Without occlude the blue shows through; with it, the overlap is red over the background.
TEST_CASE("A self-occluding blend material hides the layers behind it", "[transparent][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);

	const glm::vec4 red{ 1.0f, 0.03f, 0.03f, 0.5f };
	const glm::vec4 blue{ 0.03f, 0.03f, 1.0f, 0.5f };

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

	const auto overlapBlue = [&](bool occlude, const std::string& path) {
		auto view = gfx->CreateSceneView(scene, 8);
		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

		addPane(scene, view, blue, 0.0f, occlude);  // far
		addPane(scene, view, red, 5.0f, occlude);   // near

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, path);

		return bgl::test::MeanColor(path, 200, 300, 200, 200).b;
	};

	const float bPlain   = overlapBlue(false, "assets/golden/transparent_occlude_off.got.png");
	const float bOcclude = overlapBlue(true, "assets/golden/transparent_occlude_on.got.png");

	INFO("far-blue in overlap: plain=" << bPlain << " occlude=" << bOcclude);

	// Without occlude the far blue clearly bleeds into the overlap (a lit red pane alone carries far
	// less blue -- the rest is the environment reflecting off it).
	CHECK(bPlain > 0.35f);

	// The pre-pass stamps the near red pane's depth, so the far blue is depth-rejected: its
	// contribution to the overlap drops sharply.
	CHECK(bOcclude < bPlain * 0.65f);
}

// The transparent colour and pre-pass shaders are shared by both material types and branch on a
// per-instance discriminator. Every other transparent case here uses a baked PBR material, so the
// loose branch -- the one the Material Editor's preview actually takes -- would otherwise be
// exercised by nothing. Authoring the same material both ways must produce the same pixels; if the
// discriminator were wrong the loose render would read the other material buffer entirely.
TEST_CASE("A loose blend material renders the same as the baked one", "[transparent][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 16;
	sceneDesc.maxMeshlets             = 1024;
	sceneDesc.maxSubmeshes            = 16;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;
	sceneDesc.maxPbrMaterials         = 8;
	sceneDesc.maxLoosePbrMaterials    = 8;

	const glm::vec4 red{ 1.0f, 0.03f, 0.03f, 0.5f };
	const glm::vec4 blue{ 0.03f, 0.03f, 1.0f, 0.5f };

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

	// A scene per render, so only one of the two material buffers is ever populated. Sharing one
	// would let a read of the *wrong* buffer land on an identically-authored material and match
	// anyway -- the test would then pass even with the type discriminator broken.
	const auto render = [&](bool loose, bool occlude, const std::string& path) {
		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

		if (loose)
		{
			addLoosePane(scene, view, blue, 0.0f, occlude);
			addLoosePane(scene, view, red, 5.0f, occlude);
		}
		else
		{
			addPane(scene, view, blue, 0.0f, occlude);
			addPane(scene, view, red, 5.0f, occlude);
		}

		auto context     = bgl::RenderContext();
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		gfx->DrawFrame(target, context);
		gfx->ScreenshotPng(target, path);
	};

	SECTION("plain blend")
	{
		render(false, false, "assets/golden/transparent_uber_baked.got.png");
		render(true, false, "assets/golden/transparent_uber_loose.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/transparent_uber_baked.got.png",
				"assets/golden/transparent_uber_loose.got.png"));
	}

	// Covers the pre-pass shader too, which carries the same branch.
	SECTION("self-occluding blend")
	{
		render(false, true, "assets/golden/transparent_uber_baked_occlude.got.png");
		render(true, true, "assets/golden/transparent_uber_loose_occlude.got.png");

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/transparent_uber_baked_occlude.got.png",
				"assets/golden/transparent_uber_loose_occlude.got.png"));
	}
}
