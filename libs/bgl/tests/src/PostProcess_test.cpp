#include "util/GoldenImage.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TonemapProbe.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/BloomSettings.h>
#include <bgl/types/Camera.h>
#include <bgl/types/ColorGradeSettings.h>
#include <bgl/types/DisplayCurve.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PostProcess.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

// The post-process a target ends in, as one value. A full-frame Unlit plane puts a known radiance
// on every pixel, so the frame is the post-process's answer for it and nothing else.

namespace
{
	constexpr int c_Size = 64;

	bgl::test::GraphicsSetup
	Options()
	{
		auto opts                       = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir  = bgl::test::ShaderCacheDir();
		opts.gpuContext.clientShaderDir = "./shaders/tests/surfaces";
		return opts;
	}

	struct Plane
	{
		bgl::GraphicsRef     gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef        scene;
		bgl::RenderTargetRef target;

		Plane()
		{
			REQUIRE(gfx != nullptr);
			scene = gfx->CreateScene(bgl::SceneDesc());

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Size;
			targetDesc.height   = c_Size;
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);
		}

		/** A frame of one radiance, `exposure` applied; its mean colour. */
		bgl::test::Rgba
		Shoot(const glm::vec3& radiance, float exposure, const std::string& png)
		{
			const auto material = scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{ .surfaceName = "Unlit",
			                              .values      = { { "color", glm::vec4(radiance, 0.0f) },
			                                               { "opacity", glm::vec4(1.0f) } } });
			auto view = gfx->CreateSceneView(scene, 4);
			view->SetExposure(exposure);
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(
					scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, material)));

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f, 0.0f, 5.0f),
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			for (int i = 0; i < 2; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, png);
			return bgl::test::MeanColor(png, 16, 16, 32, 32);
		}
	};
}

TEST_CASE(
	"A target's post-process is set whole, and one created with it starts in it",
	"[tonemap][render]")
{
	Plane           plane;
	const glm::vec3 radiance(0.8f, 0.35f, 0.1f);

	const auto* plain  = "assets/golden/post_process_plain.got.png";
	const auto* graded = "assets/golden/post_process_graded.got.png";
	const auto* back   = "assets/golden/post_process_back.got.png";

	auto grade       = bgl::ColorGradeSettings();
	grade.saturation = 0.0f;

	plane.Shoot(radiance, 1.0f, plain);
	plane.target->SetPostProcess(bgl::PostProcess{ .grade = grade });
	plane.Shoot(radiance, 1.0f, graded);

	// The bloom set here is gone once a value without it is set.
	plane.target->SetPostProcess(bgl::PostProcess{ .bloom = bgl::BloomSettings() });
	plane.target->SetPostProcess(bgl::PostProcess());
	CHECK(!plane.target->GetPostProcess().bloom);
	CHECK(!plane.target->GetPostProcess().grade);
	plane.Shoot(radiance, 1.0f, back);

	CHECK(bgl::test::MaxChannelDelta(plain, graded) > 0.05f);
	CHECK(bgl::test::MaxChannelDelta(plain, back) == 0.0f);

	auto targetDesc        = bgl::RenderTargetDesc();
	targetDesc.width       = c_Size;
	targetDesc.height      = c_Size;
	targetDesc.headless    = true;
	targetDesc.postProcess = bgl::PostProcess{ .grade = grade };
	plane.target           = plane.gfx->CreateRenderTarget(targetDesc);
	CHECK(plane.target->GetPostProcess().grade.has_value());
	const auto* created = "assets/golden/post_process_created.got.png";
	plane.Shoot(radiance, 1.0f, created);
	CHECK(bgl::test::MaxChannelDelta(graded, created) == 0.0f);
}

// The curve is the target's: AgX unless it is set, Gran Turismo's when it is, and back. A colour in
// Gran Turismo's linear section is shown as it is, which AgX does not do; the probe says what each
// curve should show.
TEST_CASE("A target ends in the curve its post-process names", "[tonemap][granturismo][render]")
{
	Plane           plane;
	const glm::vec3 radiance(0.45f, 0.3f, 0.25f);

	const auto* agx     = "assets/golden/post_process_agx.got.png";
	const auto* gt      = "assets/golden/post_process_gt.got.png";
	const auto* sameAgx = "assets/golden/post_process_agx_again.got.png";

	CHECK(plane.target->GetPostProcess().curve == bgl::DisplayCurve::kAgX);
	const bgl::test::Rgba underAgx = plane.Shoot(radiance, 1.0f, agx);

	plane.target->SetPostProcess(bgl::PostProcess{ .curve = bgl::DisplayCurve::kGranTurismo });
	const bgl::test::Rgba underGt = plane.Shoot(radiance, 1.0f, gt);

	const glm::vec3 expectGt = bgl::test::RunGranTurismo(*plane.gfx, radiance);
	CHECK(underGt.r == Catch::Approx(bgl::test::EncodeSrgb(expectGt.r)).margin(1.5 / 255.0));
	CHECK(underGt.g == Catch::Approx(bgl::test::EncodeSrgb(expectGt.g)).margin(1.5 / 255.0));
	CHECK(underGt.b == Catch::Approx(bgl::test::EncodeSrgb(expectGt.b)).margin(1.5 / 255.0));
	CHECK(underGt.r == Catch::Approx(bgl::test::EncodeSrgb(radiance.r)).margin(1.5 / 255.0));

	const glm::vec3 expectAgx =
		glm::vec3(bgl::test::RunGradedAgX(*plane.gfx, radiance, glm::vec2(0.5f), {}));
	CHECK(underAgx.r == Catch::Approx(bgl::test::EncodeSrgb(expectAgx.r)).margin(1.5 / 255.0));
	CHECK(bgl::test::MaxChannelDelta(agx, gt) > 0.03f);

	plane.target->SetPostProcess(bgl::PostProcess());
	plane.Shoot(radiance, 1.0f, sameAgx);
	CHECK(bgl::test::MaxChannelDelta(agx, sameAgx) == 0.0f);

	// A value that names no curve is refused, and the target keeps the one it had.
	plane.target->SetPostProcess(bgl::PostProcess{ .curve = bgl::DisplayCurve::kGranTurismo });
	CHECK_THROWS_AS(
		plane.target->SetPostProcess(
			bgl::PostProcess{ .curve = static_cast<bgl::DisplayCurve>(uint32_t{ 7 }) }),
		bgl::GraphicsError);
	CHECK(plane.target->GetPostProcess().curve == bgl::DisplayCurve::kGranTurismo);
}
