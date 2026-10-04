#include "util/GoldenImage.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

// The post-process a target ends in: toon is the exposed value clamped and sRGB-encoded, as
// Blender's Standard view shows it, and filmic is the AgX it always was. A full-frame Unlit plane puts a
// known radiance on every pixel, so the frame is the curve's answer for it and nothing else.

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

	/** The sRGB transfer function, linear to encoded. */
	float
	SrgbEncode(float linear)
	{
		return linear <= 0.0031308f ? 12.92f * linear :
		                              1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
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
				bgl::SurfaceMaterialDesc{ .surface = "Unlit",
			                              .values  = { { "color", glm::vec4(radiance, 0.0f) },
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

TEST_CASE("Toon post-process shows the exposed value clamped and sRGB-encoded", "[tonemap][render]")
{
	Plane plane;
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kFilmic);
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kToon);

	// One quantization step, and a little for the 16-bit scene colour on the way.
	constexpr float c_Margin = 1.5f / 255.0f;

	const std::array<std::pair<glm::vec3, float>, 4> cases = { {
		{ glm::vec3(0.18f, 0.5f, 0.9f), 1.0f },
		{ glm::vec3(0.8f, 0.35f, 0.1f), 1.0f },
		{ glm::vec3(0.4f, 0.02f, 1.6f), 1.0f },
		{ glm::vec3(0.3f, 0.6f, 0.05f), 2.0f },
	} };

	for (size_t i = 0; i < cases.size(); ++i)
	{
		const auto& [radiance, exposure] = cases[i];
		INFO("case " << i);
		const auto got = plane.Shoot(
			radiance,
			exposure,
			"assets/golden/tonemap_standard_" + std::to_string(i) + ".got.png");
		const glm::vec3 exposed = glm::clamp(radiance * exposure, 0.0f, 1.0f);
		CHECK(got.r == Catch::Approx(SrgbEncode(exposed.r)).margin(c_Margin));
		CHECK(got.g == Catch::Approx(SrgbEncode(exposed.g)).margin(c_Margin));
		CHECK(got.b == Catch::Approx(SrgbEncode(exposed.b)).margin(c_Margin));
	}
}

TEST_CASE(
	"A target switches its post-process between frames, and filmic is what it was",
	"[tonemap][render]")
{
	Plane           plane;
	const glm::vec3 radiance(0.8f, 0.35f, 0.1f);

	const auto* agx      = "assets/golden/tonemap_switch_agx.got.png";
	const auto* standard = "assets/golden/tonemap_switch_standard.got.png";
	const auto* back     = "assets/golden/tonemap_switch_back.got.png";

	plane.Shoot(radiance, 1.0f, agx);
	plane.target->SetPostProcessType(bgl::PostProcessType::kToon);
	plane.Shoot(radiance, 1.0f, standard);
	plane.target->SetPostProcessType(bgl::PostProcessType::kFilmic);
	plane.Shoot(radiance, 1.0f, back);

	CHECK(bgl::test::MaxChannelDelta(agx, standard) > 0.05f);
	CHECK(bgl::test::MaxChannelDelta(agx, back) == 0.0f);

	// A target asked for toon at creation starts in it.
	auto targetDesc            = bgl::RenderTargetDesc();
	targetDesc.width           = c_Size;
	targetDesc.height          = c_Size;
	targetDesc.headless        = true;
	targetDesc.postProcessType = bgl::PostProcessType::kToon;
	plane.target               = plane.gfx->CreateRenderTarget(targetDesc);
	CHECK(plane.target->GetPostProcessType() == bgl::PostProcessType::kToon);
	const auto* created = "assets/golden/tonemap_switch_created.got.png";
	plane.Shoot(radiance, 1.0f, created);
	CHECK(bgl::test::MaxChannelDelta(standard, created) == 0.0f);
}
