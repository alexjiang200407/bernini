#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
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

	// Abutting slats in two mid greys: fine detail at moderate contrast, which is what actual scene
	// content is and what the quad cannot stand in for. Blur has to be measured against content the
	// neighbourhood clamp does not repair -- between black and white the box degenerates to the slat's
	// own colour and snaps any history back to full contrast, but between two greys the box is as wide
	// as their difference and a softened history survives inside it.
	constexpr float c_SlatWidth = 0.5f;
	constexpr int   c_SlatCount = 76;

	// Sized so the slats cover the measurement box from the drift's starting camera through its
	// arrival at x = 0.
	constexpr float c_FenceStartX = -34.0f;

	constexpr float c_SlatLight = 0.62f;
	constexpr float c_SlatDark  = 0.38f;

	// What the light slats are edited to mid-run: far enough from both to move the fence's mean well
	// clear of the frame-to-frame noise, and still inside the range the display curve resolves.
	constexpr float c_SlatEdited = 0.20f;

	bgl::PbrMaterialDesc
	Grey(float value)
	{
		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(value, value, value, 1.0f);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.6f;
		return desc;
	}

	// Takes the two materials rather than creating them, so a caller that means to edit one of them
	// mid-run holds its handle.
	void
	AddSlatWall(
		const bgl::SceneRef&     scene,
		const bgl::SceneViewRef& view,
		int                      count,
		float                    startX,
		bgl::MaterialHandle      light,
		bgl::MaterialHandle      dark)
	{
		const bgl::GeomHandle slats[] = {
			scene->AddPlaneGeom(1, 1, c_SlatWidth, c_QuadScale * 2.0f, light),
			scene->AddPlaneGeom(1, 1, c_SlatWidth, c_QuadScale * 2.0f, dark),
		};

		for (int i = 0; i < count; ++i)
		{
			const float x = startX + static_cast<float>(i) * c_SlatWidth;
			view->CreateStaticMeshInstance(
				slats[i % 2],
				glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
		}
	}

	void
	AddSlatWall(const bgl::SceneRef& scene, const bgl::SceneViewRef& view, int count, float startX)
	{
		AddSlatWall(
			scene,
			view,
			count,
			startX,
			scene->CreatePbrMaterial(Grey(c_SlatLight)),
			scene->CreatePbrMaterial(Grey(c_SlatDark)));
	}

	void
	AddFence(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(scene, view, c_SlatCount, c_FenceStartX);
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
		sceneDesc.initialPbrMaterials         = 4;
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

	using ScenePopulator = void (*)(const bgl::SceneRef&, const bgl::SceneViewRef&);
	using CameraProvider = bgl::Camera (*)(int frame);

	bgl::Camera
	StillCamera(int)
	{
		return Camera();
	}

	// Renders the populated scene for `frames` frames, each from `cameraAt(frame)`, and writes the
	// last one to `path`.
	void
	RenderTo(
		const std::string& path,
		bool               taaEnabled,
		int                frames,
		ScenePopulator     populate = AddQuad,
		CameraProvider     cameraAt = StillCamera)
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
		auto view  = gfx->CreateSceneView(scene, 128);
		populate(scene, view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = FullViewport();

		for (int frame = 0; frame < frames; ++frame)
		{
			job.camera = cameraAt(frame);
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

	// A slower, longer slide than the ghosting pan: slow enough that the quad never leaves the frame,
	// long enough that the accumulation reaches its steady state under motion rather than measuring
	// wherever the warm-up had got to.
	constexpr float c_DriftStep   = 0.5f;
	constexpr int   c_DriftFrames = 30;

	// A constant `c_DriftStep` per frame, arriving at x = 0 on the last -- which is captured while
	// the camera is still moving.
	bgl::Camera
	DriftingCamera(int frame)
	{
		return CameraAt(-static_cast<float>(c_DriftFrames - 1 - frame) * c_DriftStep);
	}

	// A quad floating in front of a wall of grey slats, for the ghost test the empty-background pan
	// cannot perform: over emptiness the neighbourhood colour clamp degenerates to the background's
	// own colour and scrubs a trail by itself, but over mid-contrast detail the clamp's box is as
	// wide as the slats' difference and a bright ghost survives inside it. Only depth can tell that
	// ghost from detail. The quad is nearer than the slats so a camera pan uncovers them by
	// parallax.
	constexpr float c_ParallaxQuadZ    = 8.0f;
	constexpr float c_ParallaxQuadSize = 6.0f;
	constexpr int   c_WallSlatCount    = 80;

	void
	AddQuadOverSlats(const bgl::SceneRef& scene, const bgl::SceneViewRef& view)
	{
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		AddSlatWall(
			scene,
			view,
			c_WallSlatCount,
			-static_cast<float>(c_WallSlatCount / 2) * c_SlatWidth);

		auto quad = scene->AddPlaneGeom(1, 1, c_ParallaxQuadSize, c_ParallaxQuadSize);
		view->CreateStaticMeshInstance(
			quad,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, c_ParallaxQuadZ)) *
				glm::rotate(glm::mat4(1.0f), glm::radians(c_QuadYaw), glm::vec3(0.0f, 0.0f, 1.0f)));
	}

	// Converges at the pan's start, then arrives at x = 0 exactly as RenderPan's pan does -- so the
	// accumulation is full of the quad when the parallax uncovers the slats behind it.
	bgl::Camera
	GhostPanCamera(int frame)
	{
		const int panFrame  = frame - c_ConvergeFrames;
		const int remaining = std::max(0, c_PanFrames - 1 - std::max(panFrame, 0));
		return CameraAt(-static_cast<float>(remaining) * c_PanStep);
	}

	bgl::Camera
	GhostStillCamera(int)
	{
		return CameraAt(0.0f);
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

// The wake over detail, as a number -- the case the empty-background pan cannot measure. Over
// emptiness the clamp's box collapses to the background's own colour and scrubs a trail
// trivially; over mid-contrast slats the box is as wide as their difference, which is where a
// bright occluder's remnant would have room to hide. It does not: wherever the wake is locally
// flat the box is tight again, and the remnant is snapped out within a frame or two of the
// reveal. The wake box is slats the quad covered at the pan's start and has fully uncovered on
// the arrival frame; the still render is the same converged scene, so anything above its own
// convergence noise is ghost.
TEST_CASE("A pan leaves no ghost on the detail it uncovers", "[taa][render]")
{
	const std::string still  = "assets/golden/taa_parallax_still.got.png";
	const std::string panned = "assets/golden/taa_parallax_panned.got.png";

	RenderTo(still, true, c_ConvergeFrames + c_PanFrames, AddQuadOverSlats, GhostStillCamera);
	RenderTo(panned, true, c_ConvergeFrames + c_PanFrames, AddQuadOverSlats, GhostPanCamera);

	// Slats the quad's right edge (at x = 199 on arrival) has left behind, inside its span at the
	// pan's start.
	constexpr int c_WakeX = 204;
	constexpr int c_WakeY = 70;
	constexpr int c_WakeW = 32;
	constexpr int c_WakeH = 116;

	const float ghost = bgl::test::FrameDelta(panned, still, c_WakeX, c_WakeY, c_WakeW, c_WakeH);

	INFO("wake delta against the converged still: " << ghost);

	// Measured 1.2e-4, which is convergence-state noise; a ghost the clamp admitted would sit an
	// order of magnitude above. Depth-based disocclusion rejection was measured against this very
	// number and moved it nowhere -- the clamp owns the wake.
	CHECK(ghost < 1.0e-3f);
}

// Blur under motion, as a number. Every off-centre history fetch low-passes the accumulation, and
// while the camera moves the fetches are off-centre every frame, so the softening compounds until
// the resolve reaches a steady state visibly blurrier than the still image -- and snaps back once
// the camera stops, which is what makes it read as motion blur rather than as the antialiasing.
//
// The instrument is the fence's alias energy, which is contrast at the pixel scale: filtering the
// accumulation too softly dims the pattern towards its mean and the energy falls. The still
// converged frame is what the resolve considers finished, so the bound is a ratio to it.
TEST_CASE("Fine detail survives the camera moving", "[taa][render]")
{
	const std::string still  = "assets/golden/taa_drift_still.got.png";
	const std::string moving = "assets/golden/taa_drift_moving.got.png";

	RenderTo(still, true, c_ConvergeFrames, AddFence);
	RenderTo(moving, true, c_DriftFrames, AddFence, DriftingCamera);

	constexpr int c_FenceBoxX = 98;
	constexpr int c_FenceBoxY = 98;
	constexpr int c_FenceBox  = 60;

	const float stillDetail =
		bgl::test::AliasEnergy(still, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);
	const float movingDetail =
		bgl::test::AliasEnergy(moving, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox);

	INFO("fence alias energy: still = " << stillDetail << ", moving = " << movingDetail);

	// The still fence has to carry real detail, or the ratio below compares noise to noise.
	CHECK(stillDetail > 3e-4f);

	// Measured 0.66 with the Catmull-Rom history fetch and 0.60 with a plain bilinear one, which is
	// the regression this pins out. The clamp bounds how much softness can survive, so the gap is
	// modest -- but it is the visible part of the moving image going soft.
	CHECK(movingDetail > stillDetail * 0.62f);
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

// A material's contents change with nothing on screen moving, and no motion vector describes that:
// the accumulation holds the surface as it was, and the resolve goes on reprojecting onto it.
//
// Fine detail is where that survives, which is why this measures the fence rather than the quad:
// over a uniform colour the neighbourhood box collapses onto the new colour and drags the
// accumulation across by itself, but between two greys the box is as wide as their difference and
// the old grey sits inside it -- and at rest the remembered spread widens it further.
TEST_CASE(
	"A material edit lands whole rather than fading in over the frames after it",
	"[taa][render]")
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

	auto scene = gfx->CreateScene(QuadSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 128);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const bgl::MaterialHandle edited = scene->CreatePbrMaterial(Grey(c_SlatLight));
	AddSlatWall(
		scene,
		view,
		c_SlatCount,
		c_FenceStartX,
		edited,
		scene->CreatePbrMaterial(Grey(c_SlatDark)));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = Camera();
	job.viewport = FullViewport();

	// Slats, several pixels wide, across the middle of the frame.
	constexpr int c_FenceBoxX = 96;
	constexpr int c_FenceBoxY = 96;
	constexpr int c_FenceBox  = 48;

	const auto drive = [&](int frames, const std::string& path) {
		for (int frame = 0; frame < frames; ++frame)
		{
			gfx->DrawFrame(target, job);
		}
		gfx->ScreenshotPng(target, path);
		return path;
	};

	const std::string before = drive(c_ConvergeFrames, "assets/golden/taa_material_before.got.png");

	scene->UpdatePbrMaterial(edited, Grey(c_SlatEdited));
	const std::string edit  = drive(1, "assets/golden/taa_material_edit.got.png");
	const std::string after = drive(c_ConvergeFrames, "assets/golden/taa_material_after.got.png");

	const float lightBefore =
		bgl::test::MeanColor(before, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();
	const float lightEdit =
		bgl::test::MeanColor(edit, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();
	const float lightAfter =
		bgl::test::MeanColor(after, c_FenceBoxX, c_FenceBoxY, c_FenceBox, c_FenceBox).Luma();

	INFO(
		"fence luma: before the edit = " << lightBefore << ", one frame after = " << lightEdit
										 << ", converged after = " << lightAfter);

	// The edit is worth measuring only if it moved the region at all.
	REQUIRE(std::abs(lightBefore - lightAfter) > 0.05f);

	// Antialiasing changes across the box's edges, not its mean, so the frame the edit lands on
	// reads as the converged one does -- unless it is still carrying the old material.
	CHECK(
		lightEdit == Catch::Approx(lightAfter).margin(std::abs(lightBefore - lightAfter) * 0.15f));
}
