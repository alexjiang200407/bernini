#include "scene/SceneView.h"
#include "util/DispatchReport.h"
#include "util/GoldenImage.h"
#include "util/PaletteReadback.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/idl/ToonShadingRigBlock.h>
#include <bgl/idl/ToonShadingRigPool.h>
#include <bgl/types/Camera.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgl/types/Viewport.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The toon shading rig in the forward pixel stage, on a synthetic head: a sphere whose rig has no
// head bone, so head space is the sphere's own. An edit's spot is found where the CPU projects it,
// against the same frame drawn with no rig; and the stage's one function is pinned by a kernel.

namespace
{
	// A character surface with its colour and how much of it is face as values.
	constexpr std::string_view c_FaceCharacter = R"(import bgl.MaterialReader;
import bgl.ToonCharacterSurface;

struct FaceParams
{
    [Color]
    [Default(0.8, 0.35, 0.1, 1.0)]
    float4 baseColorFactor;

    [Default(1.0)]
    float face;
};

struct FaceCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = FaceParams;

    static float Coverage<R : IMaterialReader>(R reader, FaceParams params) { return params.baseColorFactor.a; }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, FaceParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor;
        surface.face = params.face;
        return surface;
    }
};
)";

	std::filesystem::path
	SurfaceDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "bernini_toon_rig_surfaces";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		std::ofstream(dir / "FaceCharacter.slang", std::ios::binary) << c_FaceCharacter;
		return dir;
	}

	bgl::test::GraphicsSetup
	Options()
	{
		auto opts                                = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer         = true;
		opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.gpuContext.clientShaderDir          = SurfaceDir();
		return opts;
	}

	constexpr float c_HeadRadius = 3.0f;
	constexpr int   c_Width      = 400;
	constexpr int   c_Height     = 300;

	bgl::Camera
	FrontCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 10.0f),
				glm::vec3(0.0f, 0.0f, 9.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), float(c_Width) / float(c_Height), 0.5f, 500.0f);
		return camera;
	}

	// Where a head-space direction meets the sphere, on screen: 150 pixels are tan(30 degrees) of view.
	glm::ivec2
	ScreenOf(const glm::vec3& direction)
	{
		const glm::vec3 p            = c_HeadRadius * glm::normalize(direction);
		const float     perUnitAtOne = 0.5f * float(c_Height) / std::tan(glm::radians(30.0f));
		const float     depth        = 10.0f - p.z;
		return { int(std::lround(0.5f * c_Width + p.x * perUnitAtOne / depth)),
			     int(std::lround(0.5f * c_Height - p.y * perUnitAtOne / depth)) };
	}

	float
	LumaAround(const std::string& png, const glm::ivec2& at)
	{
		return bgl::test::MeanColor(png, at.x - 2, at.y - 2, 5, 5).Luma();
	}

	// A sun behind the head puts every face pixel in the second shade: the tone an edit's full
	// shadow takes, flat whatever the normal.
	const glm::vec3 c_Behind = glm::vec3(0.0f, 0.0f, -1.0f);

	// The two spots the edits below sit over: up-front of the sphere's right and left cheeks.
	const glm::vec3 c_Right = glm::vec3(0.4f, 0.0f, 1.0f);
	const glm::vec3 c_Left  = glm::vec3(-0.4f, 0.0f, 1.0f);

	/** A shadow 1.6 radii out along `direction`, wide enough to cover a few dozen pixels. */
	bgl::ToonShadingRigKeyDesc
	ShadowOver(const glm::vec3& light, const glm::vec3& direction)
	{
		return bgl::ToonShadingRigKeyDesc()
		    .SetLight(light)
		    .SetPosition(1.6f * c_HeadRadius * glm::normalize(direction))
		    .SetGain(-1.0f)
		    .SetSize(0.5f)
		    .SetRadius(1.5f * c_HeadRadius)
		    .SetNormalSmoothing(0.0f);
	}

	bgl::ToonShadingRigDesc
	Rig(std::vector<bgl::ToonShadingRigEditDesc> edits)
	{
		return bgl::ToonShadingRigDesc()
		    .SetHeadRadius(c_HeadRadius)
		    .SetFadeStartPixels(20.0f)
		    .SetFadeEndPixels(10.0f)
		    .SetEdits(std::move(edits));
	}

	struct World
	{
		bgl::GraphicsRef gfx = bgl::test::CreateGraphics(Options());
		bgl::SceneRef    scene;

		World()
		{
			REQUIRE(gfx != nullptr);
			REQUIRE(gfx->GetSurfaceTypes().size() == 1u);
			scene = gfx->CreateScene(bgl::SceneDesc());
		}

		bgl::MaterialHandle
		Face(float face)
		{
			return scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{ .surface = "FaceCharacter",
			                              .values  = { { "face", glm::vec4(face) } } });
		}

		/**
		 * Draws a sphere of `material` under a sun toward `toLight`, holding `rig` if it has one, and
		 * screenshots it. Returns how many placements the rig pass selected.
		 */
		uint32_t
		Shoot(
			bgl::MaterialHandle                           material,
			const std::optional<bgl::ToonShadingRigDesc>& rig,
			const glm::vec3&                              toLight,
			const char*                                   png)
		{
			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = c_Width;
			targetDesc.height   = c_Height;
			targetDesc.headless = true;
			auto target         = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto view = gfx->CreateSceneView(scene, 4);
			view->SetDirectionalLight(
				{ .direction = -glm::normalize(toLight),
			      .color     = glm::vec3(1.0f),
			      .intensity = 1.0f });
			const auto instance = view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(
					scene->AddSphereGeom(48, 48, c_HeadRadius, material)));
			if (rig.has_value())
			{
				view->SetToonShadingRig(instance, scene->AddToonShadingRig(*rig));
			}

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = FrontCamera();
			job.viewport = bgl::Viewport(float(c_Width), float(c_Height));
			for (int i = 0; i < 6; ++i)
			{
				gfx->DrawFrame(target, job);
			}
			gfx->ScreenshotPng(target, png);

			return bgl::test::ReadBuffer<bgl::idl::ToonShadingRigPool>(
					   gfx->As<bgl::GraphicsBase>(),
					   view->As<bgl::SceneView>()->GetToonShadingRigs().GetPoolBuffer(),
					   1)[0]
			    .selected;
		}
	};

	// The CPU twin of ToonShadingRigSlotOffset, line for line.
	float
	SlotOffset(const bgl::idl::ToonShadingRigSlot& slot, const glm::vec3& p, const glm::vec3& n)
	{
		const glm::vec3 d    = glm::vec3(slot.positionAndGain) - p;
		const float     dist = glm::length(d);
		if (dist < 1e-6f)
		{
			return 0.0f;
		}
		const glm::vec3 dn = d / dist;

		const float u = glm::dot(dn, glm::vec3(slot.axisXAndSize));
		const float v = glm::dot(dn, glm::vec3(slot.axisYAndAnisotropy));
		const float w = std::clamp(glm::dot(dn, glm::vec3(slot.axisZAndSharpness)), -1.0f, 1.0f);
		const float theta = std::acos(w);
		const float m = theta / (std::max(std::sqrt(u * u + v * v), 1e-6f) * slot.axisXAndSize.w);
		const float x = u * m;
		const float y = v * m;

		const float xr = x * slot.bendBulgeRotation.z - y * slot.bendBulgeRotation.w;
		const float yr = x * slot.bendBulgeRotation.w + y * slot.bendBulgeRotation.z;

		const float bend  = slot.bendBulgeRotation.x;
		const float bulge = slot.bendBulgeRotation.y;
		const float t     = 10.0f * (bulge * xr + bend * yr);
		const float x2    = bulge + std::cos(t) * (xr - bulge) - std::sin(t) * (yr - bend);
		const float y2    = bend + std::sin(t) * (xr - bulge) + std::cos(t) * (yr - bend);
		const float front = std::abs(t) < 1.5707963f ? 1.0f : 0.0f;

		const float e = std::max(1.0f - slot.axisYAndAnisotropy.w, 0.02f);
		const float g = e * x2 * x2 + std::pow(std::abs(y2), 2.0f - slot.axisZAndSharpness.w) / e;
		const float fall = std::exp(-g);

		const float radius = slot.radiusSmoothingMirror.x;
		const float s = std::clamp((radius - dist) / std::max(radius * 0.25f, 1e-4f), 0.0f, 1.0f);
		const float smooth = s * s * (3.0f - 2.0f * s);

		const float     smoothing = slot.radiusSmoothingMirror.y;
		const glm::vec3 sphere    = glm::length(p) > 1e-6f ? glm::normalize(p) : n;
		const glm::vec3 blended   = n * (1.0f - smoothing) + sphere * smoothing;
		const glm::vec3 nb        = glm::length(blended) > 1e-6f ? glm::normalize(blended) : n;
		const float     ndot      = std::max(glm::dot(nb, dn), 0.0f);

		return slot.positionAndGain.w * front * smooth * ndot * fall;
	}
}

TEST_CASE("An edit's shadow follows its keys across the face", "[toonshadingrig][render][toon]")
{
	World world;

	const glm::vec3 fromRight = glm::normalize(c_Right * glm::vec3(0.75f, 1.0f, 1.0f));
	const glm::vec3 fromLeft  = glm::normalize(c_Left * glm::vec3(0.75f, 1.0f, 1.0f));
	const auto      rig =
		Rig({ bgl::ToonShadingRigEditDesc()
	              .SetKeys({ ShadowOver(fromRight, c_Right), ShadowOver(fromLeft, c_Left) })
	              .SetKeySharpness(40.0f) });

	const auto face  = world.Face(1.0f);
	const auto right = ScreenOf(c_Right);
	const auto left  = ScreenOf(c_Left);

	const auto* shade = "assets/golden/toon_rig_keys_shade.got.png";
	world.Shoot(face, std::nullopt, c_Behind, shade);

	for (const auto& [toLight, shaded, clear, name] :
	     { std::tuple(fromRight, right, left, "right"), std::tuple(fromLeft, left, right, "left") })
	{
		INFO("the sun from the " << name);
		const std::string plain =
			std::string("assets/golden/toon_rig_keys_plain_") + name + ".got.png";
		const std::string rigged = std::string("assets/golden/toon_rig_keys_") + name + ".got.png";
		CHECK(world.Shoot(face, std::nullopt, toLight, plain.c_str()) == 0u);
		CHECK(world.Shoot(face, rig, toLight, rigged.c_str()) == 1u);

		CHECK(LumaAround(rigged, shaded) == Catch::Approx(LumaAround(shade, shaded)).margin(1e-3));
		CHECK(LumaAround(plain, shaded) > LumaAround(shade, shaded) + 0.05f);
		CHECK(LumaAround(rigged, clear) == Catch::Approx(LumaAround(plain, clear)).margin(1e-3));
	}
}

TEST_CASE("A mirrored edit shades both sides of the face", "[toonshadingrig][render][toon]")
{
	World world;

	const auto      face  = world.Face(1.0f);
	const glm::vec3 front = glm::vec3(0.0f, 0.0f, 1.0f);
	const auto      right = ScreenOf(c_Right);
	const auto      left  = ScreenOf(c_Left);

	const auto edit = bgl::ToonShadingRigEditDesc().SetKeys({ ShadowOver(front, c_Right) });

	const auto* shade    = "assets/golden/toon_rig_mirror_shade.got.png";
	const auto* plain    = "assets/golden/toon_rig_mirror_plain.got.png";
	const auto* single   = "assets/golden/toon_rig_mirror_single.got.png";
	const auto* mirrored = "assets/golden/toon_rig_mirror_both.got.png";
	world.Shoot(face, std::nullopt, c_Behind, shade);
	world.Shoot(face, std::nullopt, front, plain);
	world.Shoot(face, Rig({ edit }), front, single);
	world
		.Shoot(face, Rig({ bgl::ToonShadingRigEditDesc(edit).SetMirrored(true) }), front, mirrored);

	const auto shaded = [&](const char* png, const glm::ivec2& at) {
		return LumaAround(png, at) == Catch::Approx(LumaAround(shade, at)).margin(1e-3);
	};
	CHECK(LumaAround(plain, right) > LumaAround(shade, right) + 0.05f);
	CHECK(shaded(single, right));
	CHECK(LumaAround(single, left) == Catch::Approx(LumaAround(plain, left)).margin(1e-3));
	CHECK(shaded(mirrored, right));
	CHECK(shaded(mirrored, left));
}

TEST_CASE(
	"A rig changes nothing on a face too small, on no face, and on a PBR surface",
	"[toonshadingrig][render][toon]")
{
	World world;

	const glm::vec3 front = glm::vec3(0.0f, 0.0f, 1.0f);
	const auto      rig =
		Rig({ bgl::ToonShadingRigEditDesc()
	              .SetKeys({ ShadowOver(front, c_Right) })
	              .SetMirrored(true) });

	{
		INFO("beyond the fade's end");
		const auto  face  = world.Face(1.0f);
		const auto* plain = "assets/golden/toon_rig_small_plain.got.png";
		const auto* small = "assets/golden/toon_rig_small.got.png";
		auto        far   = rig;
		far.SetFadeStartPixels(2000.0f).SetFadeEndPixels(1000.0f);
		world.Shoot(face, std::nullopt, front, plain);
		CHECK(world.Shoot(face, far, front, small) == 0u);
		CHECK(bgl::test::MaxChannelDelta(plain, small) == 0.0f);
	}

	{
		INFO("a surface that is no face");
		const auto  body   = world.Face(0.0f);
		const auto* plain  = "assets/golden/toon_rig_body_plain.got.png";
		const auto* rigged = "assets/golden/toon_rig_body.got.png";
		world.Shoot(body, std::nullopt, front, plain);
		CHECK(world.Shoot(body, rig, front, rigged) == 1u);
		CHECK(bgl::test::MaxChannelDelta(plain, rigged) == 0.0f);
	}

	{
		// The placement is selected and its flags word carries a block, which a PBR draw's vertices
		// never read: its frame is the same bytes.
		INFO("a PBR surface");
		const auto  pbr    = world.scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		const auto* plain  = "assets/golden/toon_rig_pbr_plain.got.png";
		const auto* rigged = "assets/golden/toon_rig_pbr.got.png";
		world.Shoot(pbr, std::nullopt, front, plain);
		CHECK(world.Shoot(pbr, rig, front, rigged) == 1u);
		CHECK(bgl::test::MaxChannelDelta(plain, rigged) == 0.0f);
	}
}

TEST_CASE(
	"An edit's push on the terminator is the Shading Rig's shape",
	"[toonshadingrig][compute]")
{
	auto slot                  = bgl::idl::ToonShadingRigSlot();
	slot.positionAndGain       = glm::vec4(0.3f, 0.2f, 1.4f, -0.8f);
	const glm::vec3 lz         = glm::normalize(glm::vec3(slot.positionAndGain));
	const glm::vec3 lx         = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), lz));
	const glm::vec3 ly         = glm::cross(lz, lx);
	slot.axisXAndSize          = glm::vec4(lx, 0.35f);
	slot.axisYAndAnisotropy    = glm::vec4(ly, 0.4f);
	slot.axisZAndSharpness     = glm::vec4(lz, 0.6f);
	slot.bendBulgeRotation     = glm::vec4(0.05f, -0.03f, std::cos(0.7f), std::sin(0.7f));
	slot.radiusSmoothingMirror = glm::vec4(1.2f, 0.4f, 0.0f, 0.0f);

	struct Case
	{
		glm::vec3 p;
		glm::vec3 n;
	};
	const std::array<Case, 6> cases = { {
		{ glm::vec3(0.2f, 0.15f, 0.97f), glm::normalize(glm::vec3(0.2f, 0.15f, 0.97f)) },
		{ glm::vec3(0.35f, 0.1f, 0.93f), glm::normalize(glm::vec3(0.4f, 0.0f, 0.9f)) },
		{ glm::vec3(-0.1f, 0.3f, 0.95f), glm::normalize(glm::vec3(0.0f, 0.3f, 0.9f)) },
		{ glm::vec3(0.6f, -0.2f, 0.77f), glm::normalize(glm::vec3(0.6f, -0.2f, 0.77f)) },
		{ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
		{ glm::vec3(slot.positionAndGain), glm::vec3(0.0f, 0.0f, 1.0f) },
	} };

	constexpr uint32_t c_Stride = 128;
	static_assert(sizeof(bgl::idl::ToonShadingRigSlot) == 96);
	auto bytes = std::vector<std::byte>(cases.size() * c_Stride);
	for (size_t i = 0; i < cases.size(); ++i)
	{
		const glm::vec4 p(cases[i].p, 0.0f);
		const glm::vec4 n(cases[i].n, 0.0f);
		std::memcpy(bytes.data() + i * c_Stride, &slot, sizeof(slot));
		std::memcpy(bytes.data() + i * c_Stride + 96, &p, sizeof(p));
		std::memcpy(bytes.data() + i * c_Stride + 112, &n, sizeof(n));
	}

	const auto got = bgl::test::DispatchReport(
		"CSToonShadingRigSlotOffset",
		bytes,
		static_cast<uint32_t>(cases.size()),
		[&](bgpu::ComputeKernel& kernel) {
			kernel["gUniforms"]["caseCount"] = static_cast<uint32_t>(cases.size());
		});
	REQUIRE(got.size() == cases.size());

	auto pushes = 0;
	for (size_t i = 0; i < cases.size(); ++i)
	{
		INFO("case " << i);
		const float expected = SlotOffset(slot, cases[i].p, cases[i].n);
		CHECK(got[i].x == Catch::Approx(expected).margin(1e-4));
		pushes += std::abs(expected) > 1e-3f ? 1 : 0;
	}
	CHECK(pushes >= 3);
	CHECK(got[5].x == 0.0f);
}
