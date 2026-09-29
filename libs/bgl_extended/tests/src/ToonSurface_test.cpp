#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/SkinnedSynth.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>

using namespace bgl;

namespace
{
	// The test project's two surfaces, as a game writes them: base colour through a slot, times a
	// factor. An unbound slot samples white, so the factor alone is what draws.
	constexpr std::string_view c_ToonCharacter = R"(import bgl.MaterialReader;
import bgl.ToonCharacterSurface;

struct FlatParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct FlatCharacter : IToonCharacterSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonCharacterSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonCharacterSurface surface = ToonCharacterSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
        return surface;
    }
};
)";

	constexpr std::string_view c_ToonEnvironment = R"(import bgl.MaterialReader;
import bgl.ToonEnvironmentSurface;

struct FlatParams
{
    [Color]
    [Default(1.0, 1.0, 1.0, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct FlatEnvironment : IToonEnvironmentSurfaceSource
{
    typealias MaterialParams = FlatParams;

    static float Coverage<R : IMaterialReader>(R reader, FlatParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static ToonEnvironmentSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)
    {
        ToonEnvironmentSurface surface = ToonEnvironmentSurface();
        surface.baseColor = params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());
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

	// The two toon surfaces beside the suite's Unlit, the lit surface whose Shade is its colour
	// and nothing else -- which is what flat toon has to draw.
	std::filesystem::path
	ToonSurfaceDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "bernini_toon_surfaces";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
		Write(dir / "ToonCharacter.slang", c_ToonCharacter);
		Write(dir / "ToonEnvironment.slang", c_ToonEnvironment);
		std::filesystem::copy_file("./shaders/tests/surfaces/Unlit.slang", dir / "Unlit.slang");
		return dir;
	}

	bgl::test::GraphicsSetup
	ToonOptions()
	{
		auto opts                             = bgl::test::GraphicsSetup();
		opts.context.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.context.enableDebugLayer         = true;
		opts.context.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.context.clientShaderDir          = ToonSurfaceDir();
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
			.surface   = std::string(surface),
			.layerType = layer,
			.values    = { { "baseColorFactor", factor } },
		};
	}

	SurfaceMaterialDesc
	Unlit(glm::vec4 color, LayerType layer = LayerType::kOpaque)
	{
		return SurfaceMaterialDesc{
			.surface   = "Unlit",
			.layerType = layer,
			.values    = { { "color", glm::vec4(glm::vec3(color), 0.0f) },
			               { "opacity", glm::vec4(color.a) } },
		};
	}
}

// Registration of the toon contracts: each takes a slot in filename order beside a lit surface,
// under its own shading, with its parameters reflected exactly as any surface's are.
TEST_CASE("A toon surface registers under its own model", "[surface][registry][toon]")
{
	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);

	const std::span<const SurfaceType> types = gfx->GetSurfaceTypes();
	REQUIRE(types.size() == 3u);

	CHECK(types[0].name == "ToonCharacter");
	CHECK(types[0].kind == MaterialType::kGameStart);
	CHECK(types[0].shading == SurfaceShading::kToonCharacter);
	CHECK(types[1].name == "ToonEnvironment");
	CHECK(types[1].shading == SurfaceShading::kToonEnvironment);
	CHECK(types[2].name == "Unlit");
	CHECK(types[2].shading == SurfaceShading::kLit);

	REQUIRE(types[0].params.values.size() == 1u);
	CHECK(types[0].params.values[0].name == "baseColorFactor");
	CHECK(types[0].params.values[0].defaultValue == glm::vec4(1.0f));
	REQUIRE(types[0].params.textures.size() == 1u);
	CHECK(types[0].params.textures[0].name == "baseColor");
}

// Flat is exactly the base colour, unlit: each toon model draws what Unlit draws for the same
// colour, pixel for pixel, with a sun set that any lighting would read -- opaque, blended, and on
// skinned geometry. Each frame is a fresh view and target, so no two share temporal history, and
// each is compared against its own empty frame so an equality cannot be two blank ones.
//
// Linear rather than sectioned: a SECTION re-runs the body, and the body is a device.
TEST_CASE("A toon surface draws its base colour flat", "[surface][render][toon]")
{
	using bgl::test::skinned_synth::AddQuadStaticGeom;
	using bgl::test::skinned_synth::AddSlidingQuadGeom;

	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(ToonScene());

	const auto shoot = [&](const std::function<void(ISceneView&)>& place,
	                       const bgl::Camera&                      camera,
	                       const char*                             png) {
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = 400;
		targetDesc.height   = 300;
		targetDesc.headless = true;
		auto target         = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto view = gfx->CreateSceneView(scene, 8);
		view->SetDirectionalLight(
			{
				.direction = glm::normalize(glm::vec3(-1.0f, -0.4f, -1.0f)),
				.color     = glm::vec3(1.0f),
				.intensity = 3.0f,
			});
		place(*view);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(400.0f, 300.0f);
		for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);
	};

	const auto sphere = [&](MaterialHandle material) {
		const auto geom = scene->AddSphereGeom(24, 24, 3.5f, material);
		return [geom](ISceneView& view) { view.CreateStaticMeshInstance(geom, glm::mat4(1.0f)); };
	};

	const auto* emptyPng = "assets/golden/toon_empty.got.png";
	shoot([](ISceneView&) {}, SphereCamera(), emptyPng);

	{
		INFO("opaque");
		const auto* unlitPng       = "assets/golden/toon_unlit.got.png";
		const auto* characterPng   = "assets/golden/toon_character.got.png";
		const auto* environmentPng = "assets/golden/toon_environment.got.png";

		shoot(sphere(scene->CreateSurfaceMaterial(Unlit(c_Flat))), SphereCamera(), unlitPng);
		shoot(
			sphere(scene->CreateSurfaceMaterial(Toon("ToonCharacter", c_Flat))),
			SphereCamera(),
			characterPng);
		shoot(
			sphere(scene->CreateSurfaceMaterial(Toon("ToonEnvironment", c_Flat))),
			SphereCamera(),
			environmentPng);

		CHECK(bgl::test::FrameDelta(emptyPng, unlitPng, 0, 0, 400, 300) > 1e-3f);
		CHECK(bgl::test::FrameDelta(unlitPng, characterPng, 0, 0, 400, 300) < 1e-6f);
		CHECK(bgl::test::FrameDelta(unlitPng, environmentPng, 0, 0, 400, 300) < 1e-6f);
	}

	{
		// Blended draws resolve in the one shared transparent program, so this is each toon
		// slot's arm there -- and Shade's alpha, which the blend reads as coverage.
		INFO("blended");
		const auto* unlitPng       = "assets/golden/toon_unlit_blend.got.png";
		const auto* characterPng   = "assets/golden/toon_character_blend.got.png";
		const auto* environmentPng = "assets/golden/toon_environment_blend.got.png";

		shoot(
			sphere(scene->CreateSurfaceMaterial(Unlit(c_Half, LayerType::kBlend))),
			SphereCamera(),
			unlitPng);
		shoot(
			sphere(scene->CreateSurfaceMaterial(Toon("ToonCharacter", c_Half, LayerType::kBlend))),
			SphereCamera(),
			characterPng);
		shoot(
			sphere(
				scene->CreateSurfaceMaterial(Toon("ToonEnvironment", c_Half, LayerType::kBlend))),
			SphereCamera(),
			environmentPng);

		CHECK(bgl::test::FrameDelta(emptyPng, unlitPng, 0, 0, 400, 300) > 1e-3f);
		CHECK(bgl::test::FrameDelta(unlitPng, characterPng, 0, 0, 400, 300) < 1e-6f);
		CHECK(bgl::test::FrameDelta(unlitPng, environmentPng, 0, 0, 400, 300) < 1e-6f);
	}

	{
		// Rate 0 holds the bind pose, so the skinned geometry stage emits the static one's vertices
		// and the character surface behind both is the one record.
		INFO("skinned");
		const auto* quadEmptyPng = "assets/golden/toon_quad_empty.got.png";
		const auto* staticPng    = "assets/golden/toon_quad_static.got.png";
		const auto* skinnedPng   = "assets/golden/toon_quad_skinned.got.png";

		const MaterialHandle character =
			scene->CreateSurfaceMaterial(Toon("ToonCharacter", c_Flat));
		const auto still   = AddQuadStaticGeom(*scene, character);
		const auto skinned = AddSlidingQuadGeom(*scene, character);
		REQUIRE(still.IsValid());
		REQUIRE(skinned.IsValid());

		shoot([](ISceneView&) {}, QuadCamera(), quadEmptyPng);
		shoot(
			[&](ISceneView& view) { view.CreateStaticMeshInstance(still, glm::mat4(1.0f)); },
			QuadCamera(),
			staticPng);
		shoot(
			[&](ISceneView& view) {
				view.CreateSkinnedMeshInstance(skinned, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
			},
			QuadCamera(),
			skinnedPng);

		CHECK(bgl::test::FrameDelta(quadEmptyPng, staticPng, 0, 0, 400, 300) > 1e-3f);
		CHECK(bgl::test::FrameDelta(staticPng, skinnedPng, 0, 0, 400, 300) < 1e-6f);
	}
}

// A document's model is its contract expectation, and the toon models are two contracts: a
// material that expects one is refused a surface on the other, naming both.
TEST_CASE("A toon material is refused a surface on another model", "[surface][toon]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	auto gfx = bgl::test::CreateGraphics(ToonOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(ToonScene());

	auto expectsEnvironment    = SurfaceMaterialDesc{ .surface = "ToonCharacter" };
	expectsEnvironment.shading = SurfaceShading::kToonEnvironment;
	auto expectsCharacter      = SurfaceMaterialDesc{ .surface = "Unlit" };
	expectsCharacter.shading   = SurfaceShading::kToonCharacter;
	auto expectsItsOwn         = SurfaceMaterialDesc{ .surface = "ToonEnvironment" };
	expectsItsOwn.shading      = SurfaceShading::kToonEnvironment;

	CHECK_THROWS_MATCHES(
		scene->CreateSurfaceMaterial(expectsEnvironment),
		SceneError,
		MessageMatches(ContainsSubstring(
			"surface 'ToonCharacter' is toon-lit as a character (IToonCharacterSurfaceSource), but "
			"the material expects one that is toon-lit as an environment")));
	CHECK_THROWS_MATCHES(
		scene->CreateSurfaceMaterial(expectsCharacter),
		SceneError,
		MessageMatches(ContainsSubstring("expects one that is toon-lit as a character")));
	CHECK_NOTHROW(scene->CreateSurfaceMaterial(expectsItsOwn));
}
