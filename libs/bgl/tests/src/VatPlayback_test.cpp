#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VatSynth.h"
#include "util/VelocityReadback.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <catch2/catch_approx.hpp>

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	using namespace bgl::test::vat_synth;

	// ~52 px per world unit: focal length 300 / tan(30 deg) = 519.6 px at 10 units. A pose with
	// offset d covers x in [d-1, d+1], so each probe below sits at least 0.25 world units (13 px)
	// from every edge any asserted pose puts near it.
	float
	LumaAtWorldX(const char* png, float worldX)
	{
		const int px = static_cast<int>(std::lround(400.0f + 51.96f * worldX));
		return bgl::test::MeanColor(png, px - 6, 294, 12, 12).Luma();
	}

	glm::vec2
	ProjectToUv(const bgl::Camera& camera, glm::vec3 worldPos)
	{
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(worldPos, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * -0.5f + 0.5f);
	}
}

TEST_CASE("VAT playback follows the clock", "[vat][playback][render]")
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
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;

	const auto geom = AddSlidingQuadGeom(*scene, scene->CreatePbrMaterial(material));

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	using VatDesc = bgl::ISceneView::VatInstanceDesc;

	SECTION("a fractional phase blends the two poses it falls between")
	{
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_LoopClip, 0.5f, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_half_phase.got.png";
		gfx->ScreenshotPng(target, png);

		// Offset 0.5: covered where only the blend reaches, empty where either endpoint frame
		// alone would put geometry.
		CHECK(LumaAtWorldX(png, 1.25f) > 0.05f);   // past frame 0's right edge
		CHECK(LumaAtWorldX(png, -0.75f) < 0.01f);  // inside frame 0 only
		CHECK(LumaAtWorldX(png, 1.75f) < 0.01f);   // inside frame 1 only
	}

	SECTION("RenderJob::time advances the pose")
	{
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_LoopClip, 0.0f, 1.0f });

		job.time = 1.0f / c_SampleRate;  // exactly one frame
		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_time_frame.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(LumaAtWorldX(png, 1.75f) > 0.05f);   // frame 1's quad
		CHECK(LumaAtWorldX(png, -0.75f) < 0.01f);  // frame 0's left edge is gone
	}

	SECTION("a looping clip crosses its seam without reading the pad row")
	{
		// Phase 1.5 on a 2-frame loop: halfway from the last frame back to the first, so the pose
		// is offset 0.5. Reading forward into the pad row instead would duplicate the last frame
		// and leave the pose at offset 1 -- which the 1.75 probe would light up.
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_LoopClip, 1.5f, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_loop_seam.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(LumaAtWorldX(png, 1.25f) > 0.05f);
		CHECK(LumaAtWorldX(png, 1.75f) < 0.01f);
		CHECK(LumaAtWorldX(png, -0.75f) < 0.01f);
	}

	SECTION("a one-shot clip holds its last frame")
	{
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_ClampClip, 0.0f, 1.0f });

		job.time = 10.0f;  // 300 frames into a 2-frame clip
		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_clamp_hold.got.png";
		gfx->ScreenshotPng(target, png);

		// Held at frame 1 -- where the loop clip would have wrapped to offset 0 (10 s at 30 fps is
		// an even 300 frames), the clamp stays put.
		CHECK(LumaAtWorldX(png, 1.75f) > 0.05f);
		CHECK(LumaAtWorldX(png, -0.75f) < 0.01f);
	}

	SECTION("rate 0 holds the phase under any clock")
	{
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_LoopClip, 0.0f, 0.0f });

		job.time = 123.456f;
		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_rate_zero.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(LumaAtWorldX(png, -0.75f) > 0.05f);  // still the spawn pose
		CHECK(LumaAtWorldX(png, 1.25f) < 0.01f);

		// And no velocity: the pose at prevTime is the same pose, the camera never moved.
		job.time = 200.0f;
		gfx->DrawFrame(target, job);
		const auto motion =
			bgl::test::ReadMotionVectors(gfx.Get(), target.Get(), c_Width, c_Height);
		const glm::vec2 centre = motion[(c_Height / 2) * c_Width + c_Width / 2];
		CHECK(std::abs(centre.x) < 1e-4f);
		CHECK(std::abs(centre.y) < 1e-4f);
	}

	SECTION("an animating surface writes its own displacement")
	{
		view->CreateVatMeshInstance(geom, glm::mat4(1.0f), VatDesc{ c_LoopClip, 0.0f, 1.0f });

		// Two draws half a frame apart with a still camera: the pose moves +0.5 in X and the
		// velocity buffer must say so on its own -- prevViewProj equals viewProj, so any signal is
		// the pose reprojection, not the camera.
		job.time = 0.0f;
		gfx->DrawFrame(target, job);
		job.time = 0.5f / c_SampleRate;
		gfx->DrawFrame(target, job);

		const auto motion =
			bgl::test::ReadMotionVectors(gfx.Get(), target.Get(), c_Width, c_Height);
		const glm::vec2 measured = motion[(c_Height / 2) * c_Width + c_Width / 2];

		// The quad translates rigidly at constant depth, so every surface point's UV displacement
		// equals the projected +0.5 X shift -- derived here without the shader.
		const glm::vec2 expected = ProjectToUv(camera, glm::vec3(0.5f, 0.0f, 0.0f)) -
		                           ProjectToUv(camera, glm::vec3(0.0f, 0.0f, 0.0f));

		INFO("measured = " << measured.x << ", " << measured.y);
		INFO("expected = " << expected.x << ", " << expected.y);
		CHECK(std::abs(measured.x - expected.x) < 0.003f);
		CHECK(std::abs(measured.y - expected.y) < 0.003f);
	}
}
