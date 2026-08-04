#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

namespace
{
	constexpr uint32_t c_Width  = 256;
	constexpr uint32_t c_Height = 256;

	// Enough frames for the exponential blend to have actually settled, which is several times its
	// 1/`c_BlendWeight` time constant and not merely a couple of passes over the eight-term jitter
	// sequence. Sized for the weight the resolve ships with: too few and "converged" means "wherever
	// the accumulation had got to", which lands somewhere different for each jitter phase and makes
	// any comparison between two converged images a comparison of where they were interrupted.
	constexpr int c_ConvergeFrames = 100;

	// A quad rotated off-axis, so its edges cross pixels diagonally. An axis-aligned edge lands on a
	// pixel boundary and is already free of the stair-stepping TAA is meant to remove, which would
	// leave the antialiasing assertion below measuring nothing.
	constexpr float c_QuadYaw   = 20.0f;
	constexpr float c_QuadScale = 6.0f;
	constexpr float c_CameraZ   = 20.0f;

	bgl::Camera
	Camera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, c_CameraZ),
				glm::vec3(0.0f, 0.0f, c_CameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// The tilted quad, sized so its edges cross pixels diagonally at the camera above.
	void
	AddQuad(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		auto plane = scene->AddPlaneGeom(1, 1, c_QuadScale * 2.0f, c_QuadScale * 2.0f);
		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)));
	}

	bgl::SceneDesc
	QuadSceneDesc()
	{
		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 64;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 8192;
		sceneDesc.initialIndices              = 256;
		return sceneDesc;
	}

	bgl::GraphicsOptions
	TestOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		return opts;
	}

	bgl::Viewport
	FullViewport()
	{
		return bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
	}

	// The quad's upper-left edge, which the 20-degree roll puts on a diagonal through this box.
	constexpr int c_EdgeBoxX = 60;
	constexpr int c_EdgeBoxY = 60;
	constexpr int c_EdgeBox  = 40;

	// Renders the same tilted quad for `frames` frames and writes the last one to `path`.
	void
	RenderTo(const std::string& path, bool taaEnabled, int frames)
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 64;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 8192;
		sceneDesc.initialIndices              = 256;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 4);

		auto plane = scene->AddPlaneGeom(1, 1, c_QuadScale * 2.0f, c_QuadScale * 2.0f);
		view->CreateStaticMeshInstance(
			plane,
			glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)));

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = Camera();
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}

		gfx->ScreenshotPng(target, path);
	}

	// How far the camera slides sideways per frame while panning, in world units. Fast enough that a
	// pixel's history comes from well outside its own neighbourhood, which is the case ghosting shows
	// up in and the one the report was about.
	constexpr float c_PanStep   = 0.35f;
	constexpr int   c_PanFrames = 10;

	bgl::Camera
	CameraAt(float x)
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(x, 0.0f, c_CameraZ),
				glm::vec3(x, 0.0f, c_CameraZ - 1.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);
		return camera;
	}

	// Pans the camera to x = 0 from `c_PanTotal` to its left, and captures the frame it lands on.
	// `panning` false holds it at the destination throughout, which is the reference: the same camera,
	// the same geometry, nothing behind it.
	void
	RenderPan(const std::string& path, bool taaEnabled, bool panning)
	{
		auto gfx = bgl::CreateGraphics(TestOptions());
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = taaEnabled;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto scene = gfx->CreateScene(QuadSceneDesc());
		auto view  = gfx->CreateSceneView(scene, 4);
		AddQuad(scene, view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = FullViewport();

		for (int frame = 0; frame < c_PanFrames; ++frame)
		{
			const float offset = static_cast<float>(c_PanFrames - 1 - frame) * c_PanStep;
			job.camera         = CameraAt(panning ? -offset : 0.0f);
			gfx->DrawFrame(target, job);
		}

		gfx->ScreenshotPng(target, path);
	}
}

// The antialiasing claim, as a number. AliasEnergy is the mean squared difference between
// horizontally adjacent pixels, which is what a stair-stepped edge has and a resolved one does not.
// The box straddles the quad's tilted edge; a box wholly inside or outside it would measure a flat
// region and score near zero either way.
TEST_CASE("A converged TAA frame has less edge aliasing than an unjittered one", "[taa][render]")
{
	const std::string off = "assets/golden/taa_off.got.png";
	const std::string on  = "assets/golden/taa_converged.got.png";

	RenderTo(off, false, c_ConvergeFrames);
	RenderTo(on, true, c_ConvergeFrames);

	// The quad's upper-left edge, which the 20-degree roll puts on a diagonal through this box.
	constexpr int c_BoxX = 60;
	constexpr int c_BoxY = 60;
	constexpr int c_Box  = 40;

	const float aliased  = bgl::test::AliasEnergy(off, c_BoxX, c_BoxY, c_Box, c_Box);
	const float resolved = bgl::test::AliasEnergy(on, c_BoxX, c_BoxY, c_Box, c_Box);

	INFO("alias energy: off = " << aliased << ", converged = " << resolved);

	// The unjittered edge has to be aliased in the first place, or the comparison is vacuous.
	CHECK(aliased > 1e-3f);
	CHECK(resolved < aliased * 0.6f);
}

// Under jitter a static image must resolve to what it supersamples to, so away from the edges the
// converged frame and the unjittered one are the same picture. This is what catches a resolve that
// converges to something -- a darkened, tinted or drifting accumulation -- rather than to the truth.
TEST_CASE("A static scene converges to the unjittered image", "[taa][render]")
{
	const std::string off = "assets/golden/taa_conv_off.got.png";
	const std::string on  = "assets/golden/taa_conv_on.got.png";

	RenderTo(off, false, c_ConvergeFrames);
	RenderTo(on, true, c_ConvergeFrames);

	// Interior of the quad and a corner of the background: both flat, so jitter cannot change them
	// and any difference is the accumulation being wrong rather than antialiasing.
	const bgl::test::Rgba interiorOff = bgl::test::MeanColor(off, 118, 118, 20, 20);
	const bgl::test::Rgba interiorOn  = bgl::test::MeanColor(on, 118, 118, 20, 20);

	INFO(
		"interior: off = " << interiorOff.r << "," << interiorOff.g << "," << interiorOff.b
						   << "  on = " << interiorOn.r << "," << interiorOn.g << ","
						   << interiorOn.b);

	// Well clear of the background, or a converged-to-black bug would satisfy the equality below.
	CHECK(interiorOff.Luma() > 0.1f);

	CHECK(interiorOn.r == Catch::Approx(interiorOff.r).margin(0.02));
	CHECK(interiorOn.g == Catch::Approx(interiorOff.g).margin(0.02));
	CHECK(interiorOn.b == Catch::Approx(interiorOff.b).margin(0.02));

	const bgl::test::Rgba backgroundOn = bgl::test::MeanColor(on, 4, 4, 16, 16);
	CHECK(backgroundOn.Luma() < 0.02f);
}

// Ghosting, as a number: how much of where the camera *was* is still on screen the moment it stops.
//
// The TAA-off control is the instrument's zero. With no history the frame the pan lands on and the
// frame after holding still are the same render, so anything it scores is the measurement's own noise
// and the TAA figure has to be read against it.
//
// This is the other half of the trade. Flicker (in HashedAlpha_test) falls as the resolve leans harder
// on history; ghosting rises. Neither number means anything alone, and changing the clamp or the blend
// weight against one of them while blind to the other is how the branch got its two open complaints.
TEST_CASE("A pan leaves no more than a bounded trail behind it", "[taa][render]")
{
	const std::string reference = "assets/golden/taa_ghost_reference.got.png";
	const std::string panned    = "assets/golden/taa_ghost_panned.got.png";
	const std::string still     = "assets/golden/taa_ghost_still.got.png";

	// TAA off at the destination: what is genuinely background, decided by where the geometry is
	// rather than by how far any accumulation has got.
	RenderPan(reference, false, false);

	RenderPan(panned, true, true);
	RenderPan(still, true, false);

	const float trail = bgl::test::BackgroundBleed(panned, reference);
	const float floor = bgl::test::BackgroundBleed(still, reference);

	INFO("background bleed: arrived from a pan = " << trail << ", never moved = " << floor);

	// The same renderer that never moved is the zero. It is not exactly zero -- jitter spreads the
	// quad's edge a fraction of a pixel into its neighbours -- so the trail is read against it and not
	// against nothing.
	CHECK(floor < 2.5e-3f);

	// Measured 0.0066 against a floor of 0.0017 -- most of that gap is the resolved edge spreading a
	// fraction of a pixel, not a trail. What this actually guards is the clamp: bypassing it takes the
	// history whole and the figure goes to 0.090, so the bound is set an order of magnitude below that
	// rather than tight against the current number, which the blend weight moves by only a percent.
	CHECK(trail < 2.0e-2f);
}

// The first frame has no accumulation to blend against. If it blended anyway it would come out at
// blendWeight of its true brightness -- a tenth -- so this is the cheapest check that the
// history-invalid path exists at all.
TEST_CASE("The first TAA frame is the scene colour whole", "[taa][render]")
{
	const std::string first = "assets/golden/taa_first_frame.got.png";
	const std::string off   = "assets/golden/taa_first_off.got.png";

	RenderTo(first, true, 1);
	RenderTo(off, false, 1);

	const bgl::test::Rgba withTaa    = bgl::test::MeanColor(first, 118, 118, 20, 20);
	const bgl::test::Rgba withoutTaa = bgl::test::MeanColor(off, 118, 118, 20, 20);

	INFO("first frame luma: taa = " << withTaa.Luma() << ", off = " << withoutTaa.Luma());

	CHECK(withoutTaa.Luma() > 0.1f);
	CHECK(withTaa.Luma() == Catch::Approx(withoutTaa.Luma()).margin(0.02));
}

// The toggle exists so a temporal artifact can be judged against its absence without a restart, so
// the flag round-tripping is not the contract -- the image changing is. Turning it off must put the
// aliasing back, and turning it on again must resolve it a second time from a history that was
// discarded rather than resumed across frames that were never rendered.
TEST_CASE("Toggling temporal AA at runtime turns the resolve off and on", "[taa][render]")
{
	auto gfx = bgl::CreateGraphics(TestOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);
	CHECK(target->IsTaaEnabled());

	auto scene = gfx->CreateScene(QuadSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 4);
	AddQuad(scene, view);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = Camera();
	job.viewport = FullViewport();

	const auto drive = [&](int frames, const std::string& path) {
		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}
		gfx->ScreenshotPng(target, path);
		return bgl::test::AliasEnergy(path, c_EdgeBoxX, c_EdgeBoxY, c_EdgeBox, c_EdgeBox);
	};

	const float converged = drive(c_ConvergeFrames, "assets/golden/taa_toggle_on.got.png");

	target->SetTaaEnabled(false);
	CHECK_FALSE(target->IsTaaEnabled());

	// One frame is enough: with the resolve off there is nothing to converge, so the very next
	// frame is the raw unjittered image.
	const float disabled = drive(1, "assets/golden/taa_toggle_off.got.png");

	target->SetTaaEnabled(true);
	CHECK(target->IsTaaEnabled());

	const float reconverged = drive(c_ConvergeFrames, "assets/golden/taa_toggle_back_on.got.png");

	INFO(
		"alias energy: converged = " << converged << ", disabled = " << disabled
									 << ", reconverged = " << reconverged);

	// Off is aliased, and both on-states resolve it. The middle term is what proves the toggle does
	// something rather than leaving the resolve running.
	CHECK(disabled > converged * 1.5f);
	CHECK(reconverged < disabled * 0.6f);

	// Re-converging must reach the same place as the first time. A history that resumed across the
	// gap instead of restarting would land somewhere between the two.
	CHECK(reconverged == Catch::Approx(converged).margin(converged * 0.25f));
}

// Enabling it on a target that allocated nothing has no history to accumulate into. Silently
// ignoring the call would leave a caller believing TAA is on and wondering why the image never
// resolves, so it is a caller error rather than a no-op.
TEST_CASE("Enabling temporal AA on a target without it is an error", "[taa][render]")
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

	auto target = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);
	CHECK_FALSE(target->IsTaaEnabled());

	CHECK_THROWS_AS(target->SetTaaEnabled(true), bgl::GraphicsError);

	// Turning it off on a target that never had it is the caller asking for what it already has.
	CHECK_NOTHROW(target->SetTaaEnabled(false));
}
