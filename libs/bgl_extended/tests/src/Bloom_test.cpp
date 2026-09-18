#include "util/GoldenImage.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/error.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <cstdio>
#include <string>

// Bloom end to end: enabling it spills a bright shape's light past its silhouette, intensity
// scales the spill, a threshold above the scene kills it, and disabling restores the plain image.
// Regions of one frame are compared against each other, so there is no golden PNG to regenerate.

namespace
{
	constexpr uint32_t c_Size = 256;

	// Camera at z = c_CameraDist looking at a unit-half-extent cube: the silhouette is the front
	// face, so the left edge lands at a computable column and the spill probe sits outside it.
	// fov 60 deg vertical, tan(30 deg) below.
	constexpr float c_CameraDist = 4.0f;
	constexpr float c_TanHalfFov = 0.57735f;

	constexpr float
	EdgeColumn(uint32_t size)
	{
		const float half = static_cast<float>(size) / 2.0f;
		return half - half * (1.0f / (c_CameraDist - 1.0f)) / c_TanHalfFov;
	}

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	/**
	 * Mean luma of a box wholly outside the cube's left silhouette edge, where the frame is
	 * background. Black without bloom; anything above it is light the chain spilled there.
	 */
	float
	SpillLuma(const std::string& path, uint32_t size)
	{
		// Narrow enough to stay inside the frame at the resize test's 128 lines, where the
		// silhouette edge sits at column ~27.
		constexpr int c_ProbeWidth = 16;
		constexpr int c_ProbeRows  = 16;

		// Clear of the edge by a few columns, so the rasterized silhouette itself never lands in
		// the box however the rounding falls.
		const int x = static_cast<int>(EdgeColumn(size)) - 8 - c_ProbeWidth;
		const int y = static_cast<int>(size) / 2 - c_ProbeRows / 2;

		return bgl::test::MeanColor(path, x, y, c_ProbeWidth, c_ProbeRows).Luma();
	}

	bgl::test::Rgba
	CenterProbe(const std::string& path, uint32_t size)
	{
		const int origin = static_cast<int>(size) / 2 - 8;
		return bgl::test::MeanColor(path, origin, origin, 16, 16);
	}
}

TEST_CASE("Bloom spills a bright silhouette and honours its settings", "[bloom][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Size);
	targetDesc.height   = static_cast<int>(c_Size);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 40000;
	sceneDesc.initialIndices              = 1000;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	// No material: the kNull bucket shades the cube a uniform grey against a black background,
	// which with a zero threshold is all the chain needs.
	auto geom = scene->AddCubeGeom(bgl::MaterialHandle());
	REQUIRE(geom.IsValid());

	auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(instance.IsValid());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, c_CameraDist),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

	const auto capture = [&](const std::string& path) {
		// Two frames: the first uploads and presents, the screenshot reads the last presented.
		gfx->DrawFrame(target, job);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	// Ill-formed settings are refused before any frame runs on them.
	{
		auto bad      = bgl::BloomSettings();
		bad.intensity = -0.1f;
		REQUIRE_THROWS_AS(target->SetBloomSettings(bad), bgl::GraphicsError);

		bad           = bgl::BloomSettings();
		bad.threshold = -1.0f;
		REQUIRE_THROWS_AS(target->SetBloomSettings(bad), bgl::GraphicsError);

		bad          = bgl::BloomSettings();
		bad.softKnee = 1.5f;
		REQUIRE_THROWS_AS(target->SetBloomSettings(bad), bgl::GraphicsError);

		bad         = bgl::BloomSettings();
		bad.scatter = -0.5f;
		REQUIRE_THROWS_AS(target->SetBloomSettings(bad), bgl::GraphicsError);
	}

	const std::string offPath       = "assets/golden/bloom_off.got.png";
	const std::string lowPath       = "assets/golden/bloom_low.got.png";
	const std::string highPath      = "assets/golden/bloom_high.got.png";
	const std::string thresholdPath = "assets/golden/bloom_thresholded.got.png";
	const std::string disabledPath  = "assets/golden/bloom_disabled.got.png";

	capture(offPath);

	const auto centerOff = CenterProbe(offPath, c_Size);
	INFO("cube grey: " << centerOff.r << " " << centerOff.g << " " << centerOff.b);

	// The cube rendered -- without this the spill probes assert about an empty image.
	REQUIRE(centerOff.Luma() > 0.05f);

	const float spillOff = SpillLuma(offPath, c_Size);
	INFO("spill with bloom off: " << spillOff);
	CHECK(spillOff < 0.01f);

	// Everything blooms at threshold zero, so the grey cube is bright enough to measure with.
	auto settings      = bgl::BloomSettings();
	settings.threshold = 0.0f;
	settings.intensity = 0.25f;
	target->SetBloomSettings(settings);
	target->SetBloomEnabled(true);

	capture(lowPath);

	const float spillLow = SpillLuma(lowPath, c_Size);
	INFO("spill at intensity 0.25: " << spillLow);
	CHECK(spillLow > spillOff + 0.01f);

	settings.intensity = 1.0f;
	target->SetBloomSettings(settings);
	capture(highPath);

	const float spillHigh = SpillLuma(highPath, c_Size);
	INFO("spill at intensity 1.0: " << spillHigh);
	CHECK(spillHigh > spillLow + 0.01f);

	// A threshold above everything the scene holds blooms nothing, however strong the intensity.
	settings.threshold = 100.0f;
	settings.softKnee  = 0.0f;
	target->SetBloomSettings(settings);
	capture(thresholdPath);

	const float spillThresholded = SpillLuma(thresholdPath, c_Size);
	INFO("spill above threshold: " << spillThresholded);
	CHECK(spillThresholded < 0.01f);

	// Back to a threshold the cube passes, so the absence below measures the toggle and not the
	// threshold that already killed the spill.
	settings.threshold = 0.0f;
	target->SetBloomSettings(settings);
	target->SetBloomEnabled(false);
	capture(disabledPath);

	CHECK(SpillLuma(disabledPath, c_Size) < 0.01f);
	CHECK(!target->IsBloomEnabled());

	std::remove(offPath.c_str());
	std::remove(lowPath.c_str());
	std::remove(highPath.c_str());
	std::remove(thresholdPath.c_str());
	std::remove(disabledPath.c_str());
}

TEST_CASE("Bloom survives a resize, fed by the TAA resolve", "[bloom][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	// With TAA on, the chain reads the freshly resolved history rather than the scene colour --
	// the source ADR-4 chose, and the one barrier ordering the first test cannot exercise.
	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Size);
	targetDesc.height     = static_cast<int>(c_Size);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;
	auto target           = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 40000;
	sceneDesc.initialIndices              = 1000;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	auto geom = scene->AddCubeGeom(bgl::MaterialHandle());
	view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, c_CameraDist),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto settings      = bgl::BloomSettings();
	settings.threshold = 0.0f;
	settings.intensity = 1.0f;
	target->SetBloomSettings(settings);
	target->SetBloomEnabled(true);

	auto job   = bgl::RenderJob();
	job.view   = view;
	job.camera = camera;

	// A frame at each size, so the chain is built at the first and rebuilt after the resize.
	constexpr uint32_t c_Resized = 128;

	job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
	gfx->DrawFrame(target, job);
	gfx->DrawFrame(target, job);

	gfx->Resize(target, c_Resized, c_Resized);

	job.viewport = bgl::Viewport(static_cast<float>(c_Resized), static_cast<float>(c_Resized));
	gfx->DrawFrame(target, job);
	gfx->DrawFrame(target, job);

	const std::string resizedPath = "assets/golden/bloom_resized.got.png";
	gfx->ScreenshotPng(target, resizedPath);

	const float spill = SpillLuma(resizedPath, c_Resized);
	INFO("spill after resize: " << spill);
	CHECK(spill > 0.01f);

	std::remove(resizedPath.c_str());
}
