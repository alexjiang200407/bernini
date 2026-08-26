#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RimLightDesc.h>

namespace
{
	constexpr int c_Width  = 400;
	constexpr int c_Height = 300;

	// Two spheres of radius 4 at x = -9 and x = +9, seen from z = 30 through a 60-degree vertical
	// field: the world spans 46.2 units across the frame, so each centre lands 78 px from the
	// middle and each disc is about 35 px across.
	constexpr int c_LeftCentreX  = 122;
	constexpr int c_RightCentreX = 278;
	constexpr int c_CentreY      = 150;

	bgl::GraphicsRef
	MakeGraphics()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		return bgl::CreateGraphics(opts);
	}

	/** How red a region is, in a scene whose environment is not. */
	float
	Redness(const bgl::test::Rgba& sample) noexcept
	{
		return sample.r - sample.b;
	}

	/** The two spheres, the camera on them, and a frame drawn to convergence. */
	struct RimScene
	{
		explicit RimScene(const bgl::GraphicsRef& gfx)
		{
			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Width;
			targetDesc.height   = c_Height;
			targetDesc.headless = true;

			// On, unlike most of this suite: a rim switching on is a shading change with no motion
			// to declare it, so whether the resolve is told to drop its history is exactly what the
			// temporal case below measures -- and without an accumulation there is none to drop.
			targetDesc.taaEnabled = true;

			target = gfx->CreateRenderTarget(targetDesc);

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 512;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 800000;
			sceneDesc.initialIndices              = 20000;
			sceneDesc.initialPbrMaterials         = 4;

			scene = gfx->CreateScene(sceneDesc);
			view  = gfx->CreateSceneView(scene, 4);

			bgl::test::ApplyEnvironment(scene.Get(), view.Get());

			const auto material = scene->CreatePbrMaterial(
				{ .baseColorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f),
			      .metallicFactor  = 0.0f,
			      .roughnessFactor = 0.8f });

			const auto sphere = scene->AddSphereGeom(32, 32, 4.0f, material);

			left = view->CreateStaticMeshInstance(
				sphere,
				glm::translate(glm::mat4(1.0f), { -9.0f, 0.0f, 0.0f }));
			right = view->CreateStaticMeshInstance(
				sphere,
				glm::translate(glm::mat4(1.0f), { 9.0f, 0.0f, 0.0f }));

			camera
				.LookAt(
					glm::vec3(0.0f, 0.0f, 30.0f),
					glm::vec3(0.0f, 0.0f, 29.0f),
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.5f,
					500.0f);
		}

		/** Draws `frames` of it and screenshots to `path`; the default converges the accumulation. */
		void
		Shoot(const bgl::GraphicsRef& gfx, const std::string& path, int frames = 6) const
		{
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

			for (int i = 0; i < frames; ++i)
			{
				gfx->DrawFrame(target, job);
			}
			gfx->ScreenshotPng(target, path);
		}

		bgl::RenderTargetRef    target;
		bgl::SceneRef           scene;
		bgl::SceneViewRef       view;
		bgl::MeshInstanceHandle left;
		bgl::MeshInstanceHandle right;
		bgl::Camera             camera;
	};

	/** The mean colour of one sphere's disc in `path`. */
	bgl::test::Rgba
	Disc(const std::string& path, int centreX)
	{
		return bgl::test::MeanColor(path, centreX - 24, c_CentreY - 24, 48, 48);
	}
}

/**
 * The rim is per placement, and the same frame has to show one sphere catching it while an
 * identical one catches none.
 *
 * Each sphere is compared against *itself* with the rim off rather than against its neighbour: the
 * environment is a forest and is not left-right symmetric, so two spheres nine units apart differ
 * by a few percent in every channel before anything is rimmed. Comparing them to each other would
 * measure that.
 *
 * The tint is strongly red against an environment that is not, so what is measured is a colour the
 * scene has no other source for -- a rim term that ignored the instance flag would redden both
 * spheres, and one that was never evaluated would redden neither.
 */
TEST_CASE("Only the instance that asked for a share catches the rim light", "[rim][ibl][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	const RimScene shot(gfx);

	// A broad band and a strong tint: the measure is over a disc-sized box, so a rim narrow enough
	// to be pretty would average away against the diffuse under it.
	shot.view->SetRimLight(
		{ .tint = glm::vec3(1.0f, 0.1f, 0.1f), .intensity = 6.0f, .power = 2.0f });

	const std::string before = "assets/golden/rim_light_off.got.png";
	shot.Shoot(gfx, before);

	shot.view->SetInstanceRimIntensity(shot.left, 1.0f);
	CHECK(shot.view->GetInstanceRimIntensity(shot.left) == 1.0f);
	CHECK(shot.view->GetInstanceRimIntensity(shot.right) == 0.0f);

	const std::string after = "assets/golden/rim_light.got.png";
	shot.Shoot(gfx, after);

	const bgl::test::Rgba optedBefore = Disc(before, c_LeftCentreX);
	const bgl::test::Rgba optedAfter  = Disc(after, c_LeftCentreX);
	const bgl::test::Rgba plainBefore = Disc(before, c_RightCentreX);
	const bgl::test::Rgba plainAfter  = Disc(after, c_RightCentreX);

	// Both boxes have to be on a sphere at all, or every comparison below is between two patches of
	// background agreeing with each other.
	REQUIRE(optedBefore.Luma() > 0.02f);
	REQUIRE(plainBefore.Luma() > 0.02f);

	INFO(
		"opted redness " << Redness(optedBefore) << " -> " << Redness(optedAfter)
						 << ", plain redness " << Redness(plainBefore) << " -> "
						 << Redness(plainAfter));

	CHECK(Redness(optedAfter) - Redness(optedBefore) > 0.02f);
	CHECK(std::abs(Redness(plainAfter) - Redness(plainBefore)) < 0.005f);

	// A rim is a silhouette effect. One reaching a pixel whose normal faces the camera would be a
	// flat tint wearing its name.
	const auto centre = [](const std::string& path) {
		return bgl::test::MeanColor(path, c_LeftCentreX - 4, c_CentreY - 4, 8, 8);
	};
	CHECK(std::abs(Redness(centre(after)) - Redness(centre(before))) < 0.01f);

	std::filesystem::remove(before);
	std::filesystem::remove(after);
}

/**
 * Giving an instance a share takes effect on the next frame, not over the next several.
 *
 * Nothing about a rim switching on moves the surface, so there is no motion vector to tell the
 * temporal resolve that the pixel means something different now -- without a temporal break it
 * blends the unrimmed history into the rimmed frame and the change arrives as a fade. Six frames
 * later the two are indistinguishable, which is why the case above cannot see this and this one
 * draws exactly one frame.
 */
TEST_CASE("Changing an instance's rim intensity breaks temporal history", "[rim][ibl][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	const RimScene shot(gfx);
	shot.view->SetRimLight(
		{ .tint = glm::vec3(1.0f, 0.1f, 0.1f), .intensity = 6.0f, .power = 2.0f });

	const std::string settled = "assets/golden/rim_break_settled.got.png";
	shot.Shoot(gfx, settled);

	shot.view->SetInstanceRimIntensity(shot.left, 1.0f);

	const std::string firstFrame = "assets/golden/rim_break_first.got.png";
	shot.Shoot(gfx, firstFrame, 1);

	const std::string converged = "assets/golden/rim_break_converged.got.png";
	shot.Shoot(gfx, converged);

	const float before = Redness(Disc(settled, c_LeftCentreX));
	const float first  = Redness(Disc(firstFrame, c_LeftCentreX));
	const float after  = Redness(Disc(converged, c_LeftCentreX));

	REQUIRE(after - before > 0.02f);

	// Most of the way there on the first frame. A resolve still blending unrimmed history would
	// deliver a small fraction of it and creep up over the frames after.
	INFO("before " << before << ", first frame " << first << ", converged " << after);
	CHECK(first - before > (after - before) * 0.5f);

	std::filesystem::remove(settled);
	std::filesystem::remove(firstFrame);
	std::filesystem::remove(converged);
}

/**
 * An environment that authored no rim renders exactly as it did before there was one, opt-in or not.
 *
 * `forest.benv` carries `intensity: 0`, so every other golden in this suite depends on this holding
 * -- but they would fail confusingly, and this says which change broke them.
 */
TEST_CASE("A rim light of no intensity leaves the frame alone", "[rim][ibl][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	const RimScene shot(gfx);

	const std::string before = "assets/golden/rim_none_before.got.png";
	shot.Shoot(gfx, before);

	// A full share, and still nothing to catch: it takes both an instance that asked and an
	// environment that has a rim.
	shot.view->SetInstanceRimIntensity(shot.left, 1.0f);

	const std::string after = "assets/golden/rim_none_after.got.png";
	shot.Shoot(gfx, after);

	const bgl::test::Rgba optedBefore = Disc(before, c_LeftCentreX);
	const bgl::test::Rgba optedAfter  = Disc(after, c_LeftCentreX);

	REQUIRE(optedBefore.Luma() > 0.02f);
	CHECK(std::abs(optedAfter.r - optedBefore.r) < 0.005f);
	CHECK(std::abs(optedAfter.g - optedBefore.g) < 0.005f);
	CHECK(std::abs(optedAfter.b - optedBefore.b) < 0.005f);

	std::filesystem::remove(before);
	std::filesystem::remove(after);
}

TEST_CASE("A rim light is refused values that would blank the frame", "[rim]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 1);

	CHECK_THROWS_AS(view->SetRimLight({ .intensity = -1.0f }), bgl::SceneError);
	CHECK_THROWS_AS(
		view->SetRimLight({ .intensity = std::numeric_limits<float>::quiet_NaN() }),
		bgl::SceneError);
	CHECK_THROWS_AS(view->SetRimLight({ .power = -1.0f }), bgl::SceneError);

	CHECK_THROWS_AS(view->SetInstanceRimIntensity({}, 1.0f), bgl::SceneError);
	CHECK_THROWS_AS(view->GetInstanceRimIntensity({}), bgl::SceneError);

	const auto sphere = scene->AddSphereGeom(8, 8, 1.0f, scene->CreatePbrMaterial({}));
	const auto placed = view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));
	CHECK_THROWS_AS(view->SetInstanceRimIntensity(placed, -1.0f), bgl::SceneError);
	CHECK_THROWS_AS(
		view->SetInstanceRimIntensity(placed, std::numeric_limits<float>::quiet_NaN()),
		bgl::SceneError);
}

/**
 * An instance's share scales the view's rim rather than switching it on, which is the whole reason
 * it is a number: two units under one environment can differ in how much they catch.
 *
 * Measured as a fraction of the range rather than against a figure: the tone map compresses the
 * bright end, so a half share is not half the redness, and the exact numbers depend on the sphere's
 * material and the environment behind it. What has to hold is that it lands well inside both ends.
 */
TEST_CASE("An instance's share scales the rim it catches", "[rim][ibl][render]")
{
	auto gfx = MakeGraphics();
	REQUIRE(gfx != nullptr);

	const RimScene shot(gfx);
	shot.view->SetRimLight(
		{ .tint = glm::vec3(1.0f, 0.1f, 0.1f), .intensity = 6.0f, .power = 2.0f });

	const auto rednessAt = [&](float share, const std::string& path) {
		shot.view->SetInstanceRimIntensity(shot.left, share);
		shot.Shoot(gfx, path);
		const float redness = Redness(Disc(path, c_LeftCentreX));
		std::filesystem::remove(path);
		return redness;
	};

	const float none = rednessAt(0.0f, "assets/golden/rim_share_none.got.png");
	const float half = rednessAt(0.5f, "assets/golden/rim_share_half.got.png");
	const float full = rednessAt(1.0f, "assets/golden/rim_share_full.got.png");

	INFO("none " << none << ", half " << half << ", full " << full);
	REQUIRE(full - none > 0.01f);

	// Strictly between, not merely different: a boolean opt-in would put a half share level with
	// the full one, and a share the shader ignored would put it level with none.
	CHECK(half - none > (full - none) * 0.2f);
	CHECK(full - half > (full - none) * 0.2f);
}
