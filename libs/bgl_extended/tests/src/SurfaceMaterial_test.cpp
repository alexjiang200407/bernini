#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
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

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <utility>

using namespace bgl;

namespace
{
	bgl::GraphicsOptions
	SurfaceOptions()
	{
		auto opts                     = bgl::GraphicsOptions();
		opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer         = true;
		opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		opts.surfaceShaderDir         = "./shaders/tests/surfaces";
		return opts;
	}

	bgl::SceneDesc
	SphereScene()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 8;
		desc.initialMeshlets             = 512;
		desc.initialSubmeshes            = 8;
		desc.initialVertexBufferByteSize = 800000;
		desc.initialIndices              = 20000;
		desc.initialPbrMaterials         = 8;
		desc.initialSurfaceMaterials     = 8;
		return desc;
	}

	bgl::Camera
	SphereCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);
		return camera;
	}
}

// The gate for the whole contract. PbrLike fills PbrSurface exactly as the engine's own PBR record
// does, so the same scene drawn through a reserved game slot has to land on the engine's own answer
// -- the golden PbrRender_test writes, pixel for pixel at the suite's tolerance. Everything between
// the two is the seam this feature adds: a record packed from a reflected layout, a reader over the
// arena, a generic instantiated on a game's struct, and a row of its own.
TEST_CASE("A surface material draws what the engine's own PBR path draws", "[surface][render]")
{
	auto gfx = bgl::CreateGraphics(SurfaceOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto scene = gfx->CreateScene(SphereScene());
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	// The same numbers PbrRender_test gives CreatePbrMaterial, by name instead of by field.
	auto material = scene->CreateSurfaceMaterial(
		{
			.surface = "PbrLike",
			.values  = { { "baseColorFactor", glm::vec4(1.0f) },
	                     { "roughnessFactor", glm::vec4(0.3f) },
	                     { "metallicFactor", glm::vec4(0.6f) } },
		});

	CHECK(material.materialType == MaterialType::kGameStart);

	auto sphere = scene->AddSphereGeom(32, 32, 5.0f, material);
	view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = SphereCamera();
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

	gfx->ScreenshotPng(target, "assets/golden/surface_equivalence.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/pbr_ibl.exp.png",
			"assets/golden/surface_equivalence.got.png"));
}

// Two surfaces and three layers in one frame, which is what the reserved rows were cut for. The
// blend spheres are the load-bearing pair: every blended draw in the frame goes through the one
// shared transparent pipeline, so two game kinds in it prove the kind switch inside that program
// picks a different surface per record rather than per pipeline.
TEST_CASE("Two surfaces draw side by side across three layers", "[surface][render]")
{
	auto gfx = bgl::CreateGraphics(SurfaceOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto scene = gfx->CreateScene(SphereScene());
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	// Left: the rim opaque, every parameter set away from the defaults it declared. The emissive is
	// well over one because the environment already lights the sphere to near white, and a rim that
	// only just clears that is a rim nobody can see.
	auto rimOpaque = scene->CreateSurfaceMaterial(
		{
			.surface = "Rim",
			.values  = { { "rimColor", glm::vec4(10.0f, 3.0f, 1.0f, 0.0f) },
	                     { "rimPower", glm::vec4(2.0f) },
	                     { "baseColorFactor", glm::vec4(0.05f, 0.05f, 0.06f, 1.0f) } },
		});

	// Middle: the rim again, blended, and taking every default it declares -- so the two differ by
	// their parameters alone and a default that failed to land would show as the left one's colour.
	// Single-sided, which the mesh stage reads off GameSurfaceRecord to cull back faces: a
	// translucent sphere that kept its back hemisphere would composite that hemisphere's own
	// terminator over the front, and the faceting of it is what the golden would then pin.
	auto rimBlend = scene->CreateSurfaceMaterial(
		{
			.surface     = "Rim",
			.layerType   = LayerType::kBlend,
			.doubleSided = false,
		});

	// Right: the second surface, blended too, so both are in the one transparent dispatch. Created
	// with the wrong colour and corrected below, so the golden only matches if the update wrote the
	// record it names.
	auto tintBlend = scene->CreateSurfaceMaterial(
		{
			.surface     = "Tint",
			.layerType   = LayerType::kBlend,
			.doubleSided = false,
			.values      = { { "tint", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) } },
		});

	scene->UpdateSurfaceMaterial(
		tintBlend,
		{
			.surface     = "Tint",
			.layerType   = LayerType::kBlend,
			.doubleSided = false,
			.values      = { { "tint", glm::vec4(0.2f, 0.9f, 0.4f, 0.6f) } },
		});

	// Far right: the third layer, and the one two-sided record in the frame -- an alpha-test draw
	// keeps its back faces and its depth resolves them. Tint's coverage runs only here, and its
	// cutoff is what turns the stripes it answers with into a cutout -- so this is the pair of
	// alpha-test rows drawing.
	auto tintMask = scene->CreateSurfaceMaterial(
		{
			.surface     = "Tint",
			.layerType   = LayerType::kMask,
			.alphaCutoff = 0.5f,
			.values      = { { "tint", glm::vec4(0.9f, 0.7f, 0.1f, 1.0f) } },
		});

	CHECK(rimOpaque.materialType == rimBlend.materialType);
	CHECK(tintBlend.materialType != rimBlend.materialType);

	const std::array spheres = {
		std::pair{ scene->AddSphereGeom(24, 24, 2.4f, rimOpaque), -10.5f },
		std::pair{ scene->AddSphereGeom(24, 24, 2.4f, rimBlend), -3.5f },
		std::pair{ scene->AddSphereGeom(24, 24, 2.4f, tintBlend), 3.5f },
		std::pair{ scene->AddSphereGeom(24, 24, 2.4f, tintMask), 10.5f },
	};

	for (const auto& [geom, x] : spheres)
		view->CreateStaticMeshInstance(
			geom,
			glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = SphereCamera();
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);

	gfx->ScreenshotPng(target, "assets/golden/surface_rim.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/surface_rim.exp.png",
			"assets/golden/surface_rim.got.png"));
}

// What a material can get wrong about a surface it does not own: every name it writes was declared
// somewhere the engine only read at startup, so a name that is not there is a mistake to report and
// not a value to drop on the floor.
TEST_CASE("A surface material the engine cannot pack is refused", "[surface][render]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	auto gfx   = bgl::CreateGraphics(SurfaceOptions());
	auto scene = gfx->CreateScene(SphereScene());

	SECTION("a surface nothing registered")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial({ .surface = "Nowhere" }),
			SceneError,
			MessageMatches(ContainsSubstring("no surface named 'Nowhere' is registered")));
	}

	SECTION("a value the surface does not declare")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(
				{ .surface = "Rim", .values = { { "rimWidth", glm::vec4(1.0f) } } }),
			SceneError,
			MessageMatches(ContainsSubstring("surface 'Rim' declares no value named 'rimWidth'")));
	}

	SECTION("a texture the surface does not declare")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial({ .surface = "Rim", .textures = { { "detail", {} } } }),
			SceneError,
			MessageMatches(ContainsSubstring("surface 'Rim' declares no texture named 'detail'")));
	}

	// Declared, but as the other kind of field. Every value crosses as a vec4, so this is the one
	// shape a wrongly typed binding can take, and "no such value" would send its author looking for
	// a typo that is not there.
	SECTION("a value bound to a name the surface declared as a texture")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(
				{ .surface = "Rim", .values = { { "baseColor", glm::vec4(1.0f) } } }),
			SceneError,
			MessageMatches(
				ContainsSubstring("surface 'Rim' declares 'baseColor' as a texture, not a value")));
	}

	SECTION("a texture bound to a name the surface declared as a value")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial({ .surface = "Rim", .textures = { { "rimColor", {} } } }),
			SceneError,
			MessageMatches(
				ContainsSubstring("surface 'Rim' declares 'rimColor' as a value, not a texture")));
	}

	// An update keeps the record's kind and its size, so the surface is the one thing it cannot
	// change -- and the handle is checked before the desc, as UpdatePbrMaterial checks it.
	SECTION("an update naming a surface the material was not created with")
	{
		auto material = scene->CreateSurfaceMaterial({ .surface = "Rim" });

		CHECK_THROWS_MATCHES(
			scene->UpdateSurfaceMaterial(material, { .surface = "Tint" }),
			SceneError,
			MessageMatches(ContainsSubstring("was not created with surface 'Tint'")));
	}

	SECTION("an update on a handle that names nothing")
	{
		CHECK_THROWS_MATCHES(
			scene->UpdateSurfaceMaterial(
				MaterialHandle{ MaterialType::kGameStart, LayerType::kOpaque, 1u << 20u },
				{ .surface = "Rim" }),
			SceneError,
			MessageMatches(ContainsSubstring("is invalid or expired")));
	}

	// Hashed alpha needs the texel counts of whatever a coverage was sampled from, and a surface
	// answers with a number rather than a sample the engine can measure. So no game row draws it,
	// and the door that would create one is where that is said.
	SECTION("a layer no game row draws")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial({ .surface = "Rim", .layerType = LayerType::kHashed }),
			SceneError,
			MessageMatches(ContainsSubstring("hashed alpha, which no game row draws")));
	}
}
