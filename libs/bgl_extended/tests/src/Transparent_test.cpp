#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
	constexpr uint32_t c_Width  = 600;
	constexpr uint32_t c_Height = 800;

	// A translucent plane at world-space depth z, facing the camera.
	void
	AddPane(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		const glm::vec4&         baseColor,
		float                    z)
	{
		auto desc = bgl::PbrMaterialDesc();
		desc.baseColorFactor =
			baseColor;  // alpha < 1 drives the blend; no texture needed (white default)
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.9f;
		desc.layerType       = bgl::LayerType::kBlend;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)));
	}

	// A plane whose surface finish, layer and transmission are the variables. `metallic` at 1 drives
	// kD to zero, so the diffuse lobe carries nothing and what reaches the film is the reflection
	// alone.
	void
	AddFinishedPane(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		const glm::vec4&         baseColor,
		float                    metallic,
		float                    roughness,
		float                    transmission,
		float                    z,
		bgl::LayerType           layer)
	{
		auto desc               = bgl::PbrMaterialDesc();
		desc.baseColorFactor    = baseColor;
		desc.metallicFactor     = metallic;
		desc.roughnessFactor    = roughness;
		desc.transmissionFactor = transmission;
		desc.layerType          = layer;

		auto material = scene->CreatePbrMaterial(desc);
		auto plane    = scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, material);
		view->CreateStaticMeshInstance(plane, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z)));
	}

	// The same blend material authored the other way: a loose material routes its channels
	// explicitly, and an unrouted one falls back to exactly the PbrMaterial defaults (white for base
	// colour / ORM, flat normal for XY). So with no routes and matching factors the two must shade
	// identically -- while taking opposite branches of the shared transparent pixel shader.
	void
	AddLoosePane(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		const glm::vec4&         baseColor,
		float                    z)
	{
		auto desc            = bgl::LoosePbrMaterialDesc();
		desc.baseColorFactor = baseColor;
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.9f;
		desc.layerType       = bgl::LayerType::kBlend;

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
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
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
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		if (farFirst)
		{
			AddPane(scene, view, blue, c_FarZ);
			AddPane(scene, view, red, c_NearZ);
		}
		else
		{
			AddPane(scene, view, red, c_NearZ);
			AddPane(scene, view, blue, c_FarZ);
		}

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		gfx->DrawFrame(target, job);
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

// What the transmission factor buys, at both ends of its range.
//
// At 1 the base-colour alpha is transmission, so the reflection is light coming back off the surface
// rather than light passing through it and the alpha has no business dimming it -- a 10%-opaque lens
// under a plain src-alpha blend reflected at a tenth strength and read as a flat tint with no glint
// anywhere on it, which is the bug this pins. At 0 the alpha is coverage and must still thin the
// reflection along with everything else, which is what leaves hair, foliage and every material
// authored before the factor existed rendering as they did.
//
// A metallic pane is the instrument for both: metal drives kD to zero, so the diffuse lobe carries
// nothing and the measured luma is the reflection alone. The third measurement is the guard on the
// transmissive end -- a dielectric pane over an opaque red one, where the box sees how much of the
// backdrop survives. Transmission must not mean opacity.
TEST_CASE(
	"Transmission decides whether a blend material's alpha dims its reflection",
	"[transparent][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
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

	constexpr float c_Mirror = 0.15f;
	constexpr float c_Matte  = 0.9f;
	constexpr float c_Thin   = 0.1f;

	const glm::vec4 red{ 1.0f, 0.03f, 0.03f, 1.0f };

	// `backdrop` puts an opaque red pane behind the blended one; without it the frame is black,
	// which is what the mirror measurements want -- the box then sees the reflection and nothing
	// else. A negative `alpha` leaves the blended pane out altogether, for the bare backdrop.
	const auto renderPane = [&](const char* name,
	                            float       alpha,
	                            float       metallic,
	                            float       roughness,
	                            float       transmission,
	                            bool        backdrop) {
		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		if (backdrop)
		{
			AddFinishedPane(scene, view, red, 0.0f, c_Matte, 0.0f, 0.0f, bgl::LayerType::kOpaque);
		}

		if (alpha >= 0.0f)
		{
			AddFinishedPane(
				scene,
				view,
				glm::vec4(1.0f, 1.0f, 1.0f, alpha),
				metallic,
				roughness,
				transmission,
				5.0f,
				bgl::LayerType::kBlend);
		}

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		const auto path = std::string("assets/golden/transparent_reflection_") + name + ".got.png";
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		return bgl::test::MeanColor(path, 200, 300, 200, 200);
	};

	// The reference is opaque, where the two ends of the transmission range agree by construction.
	const float mirrorOpaque =
		renderPane("mirror_opaque", 1.0f, 1.0f, c_Mirror, 1.0f, false).Luma();
	const float mirrorGlass =
		renderPane("mirror_glass", c_Thin, 1.0f, c_Mirror, 1.0f, false).Luma();
	const float mirrorCoverage =
		renderPane("mirror_coverage", c_Thin, 1.0f, c_Mirror, 0.0f, false).Luma();

	// Blue against red, as how much of the backdrop's hue survived. Not a luma: what is on the film
	// is the AgX curve's answer, and a ratio of radiances does not survive it, while a hue does.
	// Both ends of the scale are measured rather than assumed, because neither is 0 or 1 -- the red
	// pane is lit by a whole environment, and the covering pane by the same one.
	const auto blueOverRed = [](const bgl::test::Rgba& c) { return c.b / c.r; };

	const float bare    = blueOverRed(renderPane("bare_red", -1.0f, 0.0f, c_Matte, 1.0f, true));
	const float covered = blueOverRed(renderPane("covered_red", 1.0f, 0.0f, c_Matte, 1.0f, true));
	const float thin = blueOverRed(renderPane("thin_over_red", c_Thin, 0.0f, c_Matte, 1.0f, true));

	// The references have to be lit and the hue metric has to separate its two ends, or the
	// comparisons below read noise.
	REQUIRE(mirrorOpaque > 0.02f);
	REQUIRE(covered > bare + 0.2f);

	WARN(
		"mirror luma: " << mirrorOpaque << " opaque, " << mirrorGlass << " glass, "
						<< mirrorCoverage << " coverage   blue over red: " << bare << " bare, "
						<< thin << " thin, " << covered << " covered");

	// Transmissive: a lens at a tenth opacity reflects what an opaque one does.
	CHECK(mirrorGlass > mirrorOpaque * 0.7f);

	// Coverage: the same pane with the factor at 0 is a tenth of a surface, and reflects like one.
	// 0.35 measured -- a tenth of the radiance, through AgX. This is the end every material authored
	// before the factor sits at, so it is also the assertion that says they did not move.
	CHECK(mirrorCoverage < mirrorOpaque * 0.5f);

	// And transmission is not opacity: at a tenth opacity the pane sits on the bare red pane's side
	// of the hue scale, not the covered one's. Coverage driven to 1 by the reflectance term would
	// land at the other end.
	CHECK(thin < (bare + covered) * 0.5f);
}

// The transparent colour shader is shared by both material types and branches on a
// per-instance discriminator. Every other transparent case here uses a baked PBR material, so the
// loose branch -- the one the Material Editor's preview actually takes -- would otherwise be
// exercised by nothing. Authoring the same material both ways must produce the same pixels; if the
// discriminator were wrong the loose render would read the other material buffer entirely.
TEST_CASE("A loose blend material renders the same as the baked one", "[transparent][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 16;
	sceneDesc.initialMeshlets             = 1024;
	sceneDesc.initialSubmeshes            = 16;
	sceneDesc.initialVertexBufferByteSize = 800000;
	sceneDesc.initialIndices              = 20000;
	sceneDesc.initialPbrMaterials         = 8;
	sceneDesc.initialLoosePbrMaterials    = 8;

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
	const auto render = [&](bool loose, const std::string& path) {
		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		if (loose)
		{
			AddLoosePane(scene, view, blue, 0.0f);
			AddLoosePane(scene, view, red, 5.0f);
		}
		else
		{
			AddPane(scene, view, blue, 0.0f);
			AddPane(scene, view, red, 5.0f);
		}

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	render(false, "assets/golden/transparent_uber_baked.got.png");
	render(true, "assets/golden/transparent_uber_loose.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/transparent_uber_baked.got.png",
			"assets/golden/transparent_uber_loose.got.png"));
}
