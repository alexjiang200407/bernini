#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/SkinnedSynth.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/GrassDesc.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/StaticMeshGeomDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace bgl;

namespace
{
	// A character surface as a game writes one: base colour through a slot, times a factor, and the
	// cel model's steps and threshold offset as values. An unbound slot samples white, so the
	// factor alone is the colour; the shade tints are the contract's defaults.
	constexpr std::string_view c_ToonCharacter = R"(import bgl.MaterialReader;
import bgl.ToonCharacterSurface;

struct CelParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    [Default(0.5)]
    float baseStep;

    [Default(0.3)]
    float shadeStep;

    [Default(0.0)]
    float shadeOffset;

    ColorSlot baseColor;
};

struct CelCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = CelParams;

    static float Coverage<R : IMaterialReader>(R reader, CelParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, CelParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        surface.baseStep = params.baseStep;
        surface.shadeStep = params.shadeStep;
        surface.shadeOffset = params.shadeOffset;
        return surface;
    }
};
)";

	void
	Write(const std::filesystem::path& path, std::string_view text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << text;
	}

	// The toon surface beside the suite's Unlit, the lit surface whose Shade is its colour
	// and nothing else -- which is what flat toon has to draw.
	std::filesystem::path
	ToonSurfaceDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "bernini_toon_surfaces";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		Write(dir / "ToonCharacter.slang", c_ToonCharacter);
		std::filesystem::copy_file("./shaders/tests/surfaces/Unlit.slang", dir / "Unlit.slang");
		return dir;
	}

	bgl::test::GraphicsSetup
	ToonOptions()
	{
		auto opts                                = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer         = true;
		opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.gpuContext.clientShaderDir          = ToonSurfaceDir();
		return opts;
	}

	bgl::SceneDesc
	ToonScene()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 16;
		desc.initialMeshlets             = 1024;
		desc.initialSubmeshes            = 16;
		desc.initialVertexBufferByteSize = 1600000;
		desc.initialIndices              = 40000;
		desc.initialSurfaceMaterials     = 16;
		return desc;
	}

	bgl::Camera
	SphereCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 12.0f),
				glm::vec3(0.0f, 0.0f, 11.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);
		return camera;
	}

	bgl::Camera
	QuadCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.5f, 0.0f, 6.0f),
				glm::vec3(0.5f, 0.0f, 5.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);
		return camera;
	}

	const glm::vec4 c_Flat = glm::vec4(0.8f, 0.35f, 0.1f, 1.0f);
	const glm::vec4 c_Half = glm::vec4(0.1f, 0.5f, 0.9f, 0.5f);

	SurfaceMaterialDesc
	Toon(std::string_view surface, glm::vec4 factor, LayerType layer = LayerType::kOpaque)
	{
		return SurfaceMaterialDesc{
			.surfaceName = std::string(surface),
			.layerType   = layer,
			.values      = { { "baseColorFactor", factor } },
		};
	}

	/** A character material with the cel values `values` adds to its colour. */
	SurfaceMaterialDesc
	Cel(glm::vec4                                            factor,
	    std::initializer_list<std::pair<std::string, float>> values,
	    LayerType                                            layer = LayerType::kOpaque)
	{
		auto desc = Toon("ToonCharacter", factor, layer);
		for (const auto& [name, value] : values)
		{
			desc.values.push_back({ name, glm::vec4(value) });
		}
		return desc;
	}

	SurfaceMaterialDesc
	Unlit(glm::vec4 color, LayerType layer = LayerType::kOpaque)
	{
		return SurfaceMaterialDesc{
			.surfaceName = "Unlit",
			.layerType   = layer,
			.values      = { { "color", glm::vec4(glm::vec3(color), 0.0f) },
			                 { "opacity", glm::vec4(color.a) } },
		};
	}
}

// Registration of the toon contract: it takes a slot in filename order beside a lit surface,
// under its own shading, with its parameters reflected exactly as any surface's are.
TEST_CASE("A toon surface registers under its own model", "[surface][registry][toon]")
{
	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);

	const std::span<const SurfaceType> types = gfx->GetSurfaceTypes();
	REQUIRE(types.size() == 2u);

	CHECK(types[0].surfaceName == "ToonCharacter");
	CHECK(types[0].kind == MaterialType::kGameStart);
	CHECK(types[0].shading == SurfaceShading::kToonCharacter);
	CHECK(types[1].surfaceName == "Unlit");
	CHECK(types[1].shading == SurfaceShading::kLit);

	REQUIRE(types[0].params.values.size() == 4u);
	CHECK(types[0].params.values[0].name == "baseColorFactor");
	CHECK(types[0].params.values[0].defaultValue == glm::vec4(1.0f));
	CHECK(types[0].params.values[3].name == "shadeOffset");
	REQUIRE(types[0].params.textures.size() == 1u);
	CHECK(types[0].params.textures[0].name == "baseColor");
}

// The cel model, tone by tone: a plane facing the camera has one normal, so a sun at a chosen
// angle to it puts every pixel at one half-Lambert value, and the frame is one tone. Each is
// compared against Unlit drawing that tone's colour -- base colour times its tint times the sun's
// radiance, which is one here -- through the same exposure and tonemap, in the frame's middle,
// clear of the plane's edges. Each frame is a fresh view and target, so no two share temporal
// history, and every equality is paired with a difference so it cannot be two blank frames.
//
// Linear rather than sectioned: a SECTION re-runs the body, and the body is a device.
TEST_CASE(
	"A toon character is lit in three tones over the half-Lambert term",
	"[surface][render][toon]")
{
	using bgl::test::skinned_synth::AddQuadStaticGeom;
	using bgl::test::skinned_synth::AddSlidingQuadGeom;

	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(ToonScene());

	// Toward the light, for a plane whose normal is +Z, with the half-Lambert term 0.5 * z + 0.5
	// each gives it.
	const glm::vec3 lit    = glm::vec3(0.0f, 0.0f, 1.0f);      // 1.0
	const glm::vec3 nearly = glm::vec3(0.9165f, 0.0f, 0.4f);   // 0.7
	const glm::vec3 first  = glm::vec3(0.9798f, 0.0f, -0.2f);  // 0.4
	const glm::vec3 second = glm::vec3(0.6f, 0.0f, -0.8f);     // 0.1
	const glm::vec3 behind = glm::vec3(0.0f, 0.0f, -1.0f);     // 0.0

	const auto shoot = [&](const std::function<void(ISceneView&)>& place,
	                       const glm::vec3&                        toLight,
	                       const bgl::Camera&                      camera,
	                       const char*                             png) {
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = 400;
		targetDesc.height   = 300;
		targetDesc.headless = true;
		auto target         = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto view = gfx->CreateSceneView(scene, 8);
		view->SetToonDirectionalLight(
			{
				.direction = -glm::normalize(toLight),
				.color     = glm::vec3(1.0f),
				.intensity = 1.0f,
			});
		place(*view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(400.0f, 300.0f);
		for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);
	};

	const auto plane = [&](MaterialHandle material) {
		const auto geom = scene->AddPlaneGeom(1, 1, 30.0f, 30.0f, material);
		return [geom](ISceneView& view) {
			view.CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(geom));
		};
	};

	const auto middle = [](const char* a, const char* b) {
		return bgl::test::FrameDelta(a, b, 150, 100, 100, 100);
	};
	// Mean squared, in [0, 1] per channel: under one 8-bit step apart, and several steps apart.
	constexpr float c_Same  = 1e-5f;
	constexpr float c_Apart = 1e-3f;

	const glm::vec3 base   = glm::vec3(c_Flat);
	const auto      tone   = [&](const glm::vec3& tint) { return glm::vec4(base * tint, 1.0f); };
	const glm::vec3 shade1 = glm::vec3(0.75f);
	const glm::vec3 shade2 = glm::vec3(0.55f);

	const MaterialHandle character = scene->CreateSurfaceMaterial(Cel(c_Flat, {}));

	const auto* litRef    = "assets/golden/toon_cel_lit_ref.got.png";
	const auto* firstRef  = "assets/golden/toon_cel_first_ref.got.png";
	const auto* secondRef = "assets/golden/toon_cel_second_ref.got.png";
	shoot(
		plane(scene->CreateSurfaceMaterial(Unlit(tone(glm::vec3(1.0f))))),
		lit,
		SphereCamera(),
		litRef);
	shoot(plane(scene->CreateSurfaceMaterial(Unlit(tone(shade1)))), lit, SphereCamera(), firstRef);
	shoot(plane(scene->CreateSurfaceMaterial(Unlit(tone(shade2)))), lit, SphereCamera(), secondRef);

	{
		INFO("three tones");
		const auto* litPng    = "assets/golden/toon_cel_lit.got.png";
		const auto* firstPng  = "assets/golden/toon_cel_first.got.png";
		const auto* secondPng = "assets/golden/toon_cel_second.got.png";

		shoot(plane(character), lit, SphereCamera(), litPng);
		shoot(plane(character), first, SphereCamera(), firstPng);
		shoot(plane(character), second, SphereCamera(), secondPng);

		CHECK(middle(litPng, litRef) < c_Same);
		CHECK(middle(firstPng, firstRef) < c_Same);
		CHECK(middle(secondPng, secondRef) < c_Same);
		CHECK(middle(litPng, firstPng) > c_Apart);
		CHECK(middle(firstPng, secondPng) > c_Apart);

		// Anywhere above the base step is the one lit tone: a cel step, not a gradient.
		const auto* nearlyPng = "assets/golden/toon_cel_nearly.got.png";
		shoot(plane(character), nearly, SphereCamera(), nearlyPng);
		CHECK(middle(nearlyPng, litRef) < c_Same);
	}

	{
		INFO("a threshold offset moves a lit pixel into the first shade");
		const auto* offsetPng = "assets/golden/toon_cel_offset.got.png";
		shoot(
			plane(scene->CreateSurfaceMaterial(Cel(c_Flat, { { "shadeOffset", -0.3f } }))),
			nearly,
			SphereCamera(),
			offsetPng);
		CHECK(middle(offsetPng, firstRef) < c_Same);
	}

	{
		INFO("steps of zero light even the side facing away");
		const auto* behindPng = "assets/golden/toon_cel_behind.got.png";
		const auto* alwaysPng = "assets/golden/toon_cel_always.got.png";
		shoot(plane(character), behind, SphereCamera(), behindPng);
		shoot(
			plane(scene->CreateSurfaceMaterial(
				Cel(c_Flat, { { "baseStep", 0.0f }, { "shadeStep", 0.0f } }))),
			behind,
			SphereCamera(),
			alwaysPng);
		CHECK(middle(behindPng, secondRef) < c_Same);
		CHECK(middle(alwaysPng, litRef) < c_Same);
	}

	{
		// Blended draws resolve in the one shared transparent program, so this is the character
		// slot's arm there -- and Shade's alpha, which the blend reads as coverage.
		INFO("blended");
		const auto* blendPng = "assets/golden/toon_cel_blend.got.png";
		const auto* blendRef = "assets/golden/toon_cel_blend_ref.got.png";
		const auto* emptyPng = "assets/golden/toon_cel_blend_empty.got.png";
		shoot(
			plane(scene->CreateSurfaceMaterial(Cel(c_Half, {}, LayerType::kBlend))),
			second,
			SphereCamera(),
			blendPng);
		shoot(
			plane(scene->CreateSurfaceMaterial(
				Unlit(glm::vec4(glm::vec3(c_Half) * shade2, c_Half.a), LayerType::kBlend))),
			second,
			SphereCamera(),
			blendRef);
		shoot([](ISceneView&) {}, second, SphereCamera(), emptyPng);
		CHECK(middle(blendPng, blendRef) < c_Same);
		CHECK(middle(blendPng, emptyPng) > c_Apart);
	}

	{
		// Rate 0 holds the bind pose, so the skinned geometry stage emits the static one's vertices
		// -- normals included -- and the character surface behind both is the one record.
		INFO("skinned");
		const auto* quadEmptyPng = "assets/golden/toon_quad_empty.got.png";
		const auto* staticPng    = "assets/golden/toon_quad_static.got.png";
		const auto* skinnedPng   = "assets/golden/toon_quad_skinned.got.png";

		const auto still   = AddQuadStaticGeom(*scene, character);
		const auto skinned = AddSlidingQuadGeom(*scene, character);
		REQUIRE(still.IsValid());
		REQUIRE(skinned.IsValid());

		shoot([](ISceneView&) {}, first, QuadCamera(), quadEmptyPng);
		shoot(
			[&](ISceneView& view) {
				view.CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(still));
			},
			first,
			QuadCamera(),
			staticPng);
		shoot(
			[&](ISceneView& view) {
				view.CreateSkinnedMeshInstance(
					bgl::SkinnedMeshInstanceDesc().SetGeom(skinned).SetPlayback(
						bgl::SkinnedPlaybackDesc::FromClip(0, 0.0f, 0.0f)));
			},
			first,
			QuadCamera(),
			skinnedPng);

		CHECK(bgl::test::FrameDelta(quadEmptyPng, staticPng, 0, 0, 400, 300) > 1e-3f);
		CHECK(bgl::test::FrameDelta(staticPng, skinnedPng, 0, 0, 400, 300) < 1e-6f);
	}
}

// The two suns are two: a toon character is lit by the toon sun alone and a PBR surface by the PBR
// sun alone, neither falling back on the other.
TEST_CASE("A toon character and a PBR surface each read their own sun", "[surface][render][toon]")
{
	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(ToonScene());

	const auto shoot =
		[&](MaterialHandle material, float pbrIntensity, float toonIntensity, const char* png) {
			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = 400;
			targetDesc.height   = 300;
			targetDesc.headless = true;
			auto target         = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto view = gfx->CreateSceneView(scene, 8);
			view->SetPbrDirectionalLight(
				{ .direction = glm::vec3(0.0f, 0.0f, -1.0f), .intensity = pbrIntensity });
			view->SetToonDirectionalLight(
				{ .direction = glm::vec3(0.0f, 0.0f, -1.0f), .intensity = toonIntensity });
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(
					scene->AddPlaneGeom(1, 1, 30.0f, 30.0f, material)));

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = SphereCamera();
			job.viewport = bgl::Viewport(400.0f, 300.0f);
			for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, png);
		};

	const auto middle = [](const char* png) {
		return bgl::test::MeanColor(png, 150, 100, 100, 100).Luma();
	};

	{
		INFO("a toon character");
		const auto  character = scene->CreateSurfaceMaterial(Cel(c_Flat, {}));
		const auto* toonOnly  = "assets/golden/toon_suns_character_toon.got.png";
		const auto* pbrOnly   = "assets/golden/toon_suns_character_pbr.got.png";
		shoot(character, 0.0f, 1.0f, toonOnly);
		shoot(character, 1.0f, 0.0f, pbrOnly);
		CHECK(middle(toonOnly) > 0.1f);
		CHECK(middle(pbrOnly) < 1e-3f);
	}

	{
		INFO("a PBR surface");
		const auto  pbr      = scene->CreatePbrMaterial(bgl::PbrMaterialDesc());
		const auto* neither  = "assets/golden/toon_suns_pbr_none.got.png";
		const auto* toonOnly = "assets/golden/toon_suns_pbr_toon.got.png";
		const auto* pbrOnly  = "assets/golden/toon_suns_pbr_pbr.got.png";
		shoot(pbr, 0.0f, 0.0f, neither);
		shoot(pbr, 0.0f, 1.0f, toonOnly);
		shoot(pbr, 1.0f, 0.0f, pbrOnly);
		CHECK(bgl::test::MaxChannelDelta(neither, toonOnly) == 0.0f);
		CHECK(middle(pbrOnly) > middle(neither) + 0.05f);
	}
}

namespace
{
	// One quad in the z = 0 plane, spanning x0..x1 and -1..1, as the next submesh of `mesh`. No
	// normal attribute: a decoded vertex then faces +Z.
	void
	AppendQuad(assetlib::BMesh& mesh, float x0, float x1)
	{
		constexpr uint16_t c_Stride = 12;

		const std::array<glm::vec3, 4> corners = {
			glm::vec3(x0, -1.0f, 0.0f),
			glm::vec3(x1, -1.0f, 0.0f),
			glm::vec3(x1, 1.0f, 0.0f),
			glm::vec3(x0, 1.0f, 0.0f),
		};
		const auto byteOffset = static_cast<uint32_t>(mesh.vertexData.size());
		mesh.vertexData.resize(byteOffset + sizeof(corners));
		std::memcpy(mesh.vertexData.data() + byteOffset, corners.data(), sizeof(corners));

		auto meshlet            = assetlib::Meshlet();
		meshlet.vertexOffset    = static_cast<uint32_t>(mesh.meshletVertices.size());
		meshlet.triangleOffset  = static_cast<uint32_t>(mesh.meshletTriangles.size());
		meshlet.vertexCount     = 4;
		meshlet.triangleCount   = 2;
		meshlet.boundingRadius  = 2.0f;
		const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
		mesh.meshlets.push_back(meshlet);
		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		constexpr std::array<uint8_t, 6> c_Triangles = { { 0, 1, 2, 0, 2, 3 } };
		mesh.meshletTriangles.insert(
			mesh.meshletTriangles.end(),
			c_Triangles.begin(),
			c_Triangles.end());

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexByteOffset      = byteOffset;
		submesh.vertexCount           = 4;
		submesh.firstMeshlet          = firstMeshlet;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(x0, -1.0f, 0.0f);
		submesh.aabbMax               = glm::vec3(x1, 1.0f, 0.0f);
		mesh.submeshes.push_back(submesh);
	}

	/** Two levels: level 0 a quad left of centre, level 1 one right of it. */
	assetlib::BMesh
	MakeSplitLevels()
	{
		auto mesh = assetlib::BMesh();
		AppendQuad(mesh, -1.0f, -0.1f);
		AppendQuad(mesh, 0.1f, 1.0f);
		mesh.meshes.push_back(
			assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0, .lodCount = 2 });
		mesh.lods = { { 20.0f }, { 0.0f } };
		return mesh;
	}

	/** Which of the two suns a frame turns on. */
	enum class Sun
	{
		kToon,
		kPbr,
	};

	void
	LightBy(bgl::ISceneView& view, const Sun sun, const glm::vec3& direction)
	{
		const auto on  = bgl::DirectionalLightDesc{ .direction = direction, .intensity = 1.0f };
		const auto off = bgl::DirectionalLightDesc{ .direction = direction, .intensity = 0.0f };
		view.SetToonDirectionalLight(sun == Sun::kToon ? on : off);
		view.SetPbrDirectionalLight(sun == Sun::kPbr ? on : off);
	}
}

// The toon sun reaches every lane a toon character draws through, not the one at rest alone: a
// level dissolving and the shared blend program each light the character by the toon sun and draw
// it black by the PBR sun. A binding missed in one of them draws black as well,
// so each is shown lit.
TEST_CASE("A toon character's every lane reads the toon sun", "[surface][render][toon]")
{
	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(ToonScene());

	const MaterialHandle character = scene->CreateSurfaceMaterial(Cel(c_Flat, {}));

	{
		// The placement stands at distance 2 and spans about 35 pixels: a threshold scale of 4 puts
		// it at level 1, and 1 dissolves it to level 0 over five frames, both halves drawing.
		INFO("dissolving between levels");
		const auto levels    = MakeSplitLevels();
		const auto materials = std::array<MaterialHandle, 1>{ { character } };
		const auto geom      = scene->AddStaticMeshGeom(
			bgl::StaticMeshGeomDesc().SetMesh(&levels).SetMaterials(materials));
		REQUIRE(geom.IsValid());

		const auto dissolve = [&](const Sun sun, const char* png) {
			auto targetDesc       = bgl::RenderTargetDesc();
			targetDesc.width      = 64;
			targetDesc.height     = 64;
			targetDesc.headless   = true;
			targetDesc.taaEnabled = false;
			auto target           = gfx->CreateRenderTarget(targetDesc);
			auto view             = gfx->CreateSceneView(scene, 4);
			LightBy(*view, sun, glm::vec3(0.0f, 0.0f, -1.0f));
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(geom).SetTransform(
					glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f))));

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.viewport = bgl::Viewport(64.0f, 64.0f);
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 0.0f, -1.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

			auto selection       = bgl::LodSelectionDesc();
			selection.pixelScale = 4.0f;
			view->SetLodSelection(selection);
			gfx->DrawFrame(target, job);

			selection.pixelScale = 1.0f;
			view->SetLodSelection(selection);
			for (int frame = 1; frame <= 2; ++frame)
			{
				job.time += 0.03f;
				gfx->DrawFrame(target, job);
			}
			gfx->ScreenshotPng(target, png);

			return std::pair(
				bgl::test::MeanColor(png, 18, 20, 10, 24).r,
				bgl::test::MeanColor(png, 36, 20, 10, 24).r);
		};

		const auto [toonNear, toonFar] =
			dissolve(Sun::kToon, "assets/golden/toon_lanes_dissolve_toon.got.png");
		const auto [pbrNear, pbrFar] =
			dissolve(Sun::kPbr, "assets/golden/toon_lanes_dissolve_pbr.got.png");
		CHECK(toonNear > 0.05f);
		CHECK(toonFar > 0.05f);
		CHECK(pbrNear < 1e-3f);
		CHECK(pbrFar < 1e-3f);
	}

	{
		INFO("blended");
		const auto blended = scene->CreateSurfaceMaterial(Cel(c_Half, {}, LayerType::kBlend));
		const auto plane   = scene->AddPlaneGeom(1, 1, 30.0f, 30.0f, blended);

		const auto blend = [&](const Sun sun, const char* png) {
			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = 400;
			targetDesc.height   = 300;
			targetDesc.headless = true;
			auto target         = gfx->CreateRenderTarget(targetDesc);
			auto view           = gfx->CreateSceneView(scene, 4);
			LightBy(*view, sun, glm::vec3(0.0f, 0.0f, -1.0f));
			view->CreateStaticMeshInstance(bgl::StaticMeshInstanceDesc().SetGeom(plane));

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = SphereCamera();
			job.viewport = bgl::Viewport(400.0f, 300.0f);
			for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
			gfx->ScreenshotPng(target, png);
			return bgl::test::MeanColor(png, 150, 100, 100, 100).b;
		};

		CHECK(blend(Sun::kToon, "assets/golden/toon_lanes_blend_toon.got.png") > 0.05f);
		CHECK(blend(Sun::kPbr, "assets/golden/toon_lanes_blend_pbr.got.png") < 1e-3f);
	}
}

// Grass is environment, which shades PBR: a toon character's programs read a placement's rig, which
// a blade has none of, so a look is refused one whether it is made with it or changed to it.
TEST_CASE("A grass look refuses a toon character surface", "[surface][grass][toon]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(ToonScene());

	const auto character = scene->CreateSurfaceMaterial(Cel(c_Flat, {}));
	auto       look      = bgl::GrassDesc();
	look.material        = character;
	CHECK_THROWS_MATCHES(
		scene->CreateGrass(look),
		SceneError,
		MessageMatches(ContainsSubstring("CreateGrass: a toon character surface")));

	look.material    = scene->CreatePbrMaterial(bgl::PbrMaterialDesc{});
	const auto grass = scene->CreateGrass(look);
	look.material    = character;
	CHECK_THROWS_MATCHES(
		scene->UpdateGrass(grass, look),
		SceneError,
		MessageMatches(ContainsSubstring("UpdateGrass: a toon character surface")));
}

// A document's model is its contract expectation: a material that expects the toon model is
// refused a surface on another, and a toon surface is refused a material expecting another model.
TEST_CASE("A toon material is refused a surface on another model", "[surface][toon]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(ToonScene());

	auto expectsLit          = SurfaceMaterialDesc{ .surfaceName = "ToonCharacter" };
	expectsLit.shading       = SurfaceShading::kLit;
	auto expectsCharacter    = SurfaceMaterialDesc{ .surfaceName = "Unlit" };
	expectsCharacter.shading = SurfaceShading::kToonCharacter;
	auto expectsItsOwn       = SurfaceMaterialDesc{ .surfaceName = "ToonCharacter" };
	expectsItsOwn.shading    = SurfaceShading::kToonCharacter;

	CHECK_THROWS_MATCHES(
		scene->CreateSurfaceMaterial(expectsLit),
		SceneError,
		MessageMatches(ContainsSubstring(
			"surface 'ToonCharacter' is toon-lit as a character (IToonCharacterSurfaceSource), but "
			"the material expects one that owns its lighting")));
	CHECK_THROWS_MATCHES(
		scene->CreateSurfaceMaterial(expectsCharacter),
		SceneError,
		MessageMatches(ContainsSubstring("expects one that is toon-lit as a character")));
	CHECK_NOTHROW(scene->CreateSurfaceMaterial(expectsItsOwn));
}
