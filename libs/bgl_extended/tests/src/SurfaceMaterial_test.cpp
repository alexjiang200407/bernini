#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/SkinnedSynth.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/GeomType.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/SurfaceType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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

namespace
{
	// One flat, emissive surface: black base, so what lands on screen is the emissive colour and
	// nothing the environment reflects could be mistaken for it. Every one declares the same struct
	// name, so the programs generated for them must keep each surface behind its own slot rather
	// than import the six side by side.
	std::string
	EmissiveSurface(const glm::vec3 colour)
	{
		return std::format(
			"import bgl.MaterialReader;\nimport bgl.PbrSurface;\nimport bgl.SurfaceSource;\n\n"
			"struct FlatParams {{ float unused; }};\n\n"
			"struct FlatSurface : ISurfaceSource\n{{\n"
			"    typealias MaterialParams = FlatParams;\n"
			"    static float Coverage<R : IMaterialReader>(R reader, FlatParams params) {{ return "
			"1.0; }}\n"
			"    static PbrSurface Evaluate<R : IMaterialReader>(R reader, FlatParams params)\n    "
			"{{\n"
			"        PbrSurface surface = PbrSurface();\n"
			"        surface.baseColor = float4(0.0, 0.0, 0.0, 1.0);\n"
			"        surface.orm = float3(1.0, 1.0, 0.0);\n"
			"        surface.emissive = float3({}, {}, {});\n"
			"        return surface;\n    }}\n}};\n",
			colour.r,
			colour.g,
			colour.b);
	}
}

// More surfaces than the engine once reserved, all drawing in one frame: each through its own
// generated colour program (top row) and every one through the one generated blend program (bottom
// row). The blend row is the case the old four-arm switch got silently wrong -- it sent any kind past
// the fourth to the fourth surface -- so every sphere is checked for its own surface's colour.
TEST_CASE("More than four surfaces draw, opaque and blended", "[surface][render]")
{
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "bernini_surfaces_six_draw";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	// Saturated, far apart, and dim enough that the tonemap keeps them saturated: the check is which
	// channels dominate, not a tolerance on a value the tonemap has had its way with.
	const std::array<glm::vec3, 6> colours = { {
		{ 0.5f, 0.0f, 0.0f },
		{ 0.0f, 0.5f, 0.0f },
		{ 0.0f, 0.0f, 0.5f },
		{ 0.5f, 0.5f, 0.0f },
		{ 0.0f, 0.5f, 0.5f },
		{ 0.5f, 0.0f, 0.5f },
	} };

	for (size_t i = 0; i < colours.size(); ++i)
	{
		std::ofstream out(dir / std::format("Flat{}.slang", i), std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << EmissiveSurface(colours[i]);
	}

	auto opts             = SurfaceOptions();
	opts.surfaceShaderDir = dir;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);
	REQUIRE(gfx->GetSurfaceTypes().size() == colours.size());

	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = SphereScene();
	sceneDesc.initialGeom             = 16;
	sceneDesc.initialSubmeshes        = 16;
	sceneDesc.initialSurfaceMaterials = 16;
	auto scene                        = gfx->CreateScene(sceneDesc);
	auto view                         = gfx->CreateSceneView(scene, 16);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const bgl::Camera camera = SphereCamera();

	struct Sphere
	{
		glm::vec3 centre;
		size_t    surface;
	};
	std::vector<Sphere> spheres;

	for (size_t i = 0; i < colours.size(); ++i)
	{
		const std::string name = std::format("Flat{}", i);
		const float       x    = -12.5f + 5.0f * static_cast<float>(i);

		const auto opaque = scene->CreateSurfaceMaterial({ .surface = name });
		const auto blend  = scene->CreateSurfaceMaterial(
			{ .surface = name, .layerType = LayerType::kBlend, .doubleSided = false });

		for (const auto& [material, y] : { std::pair{ opaque, 4.0f }, std::pair{ blend, -4.0f } })
		{
			const glm::vec3 centre(x, y, 0.0f);
			const auto      geom = scene->AddSphereGeom(16, 16, 1.8f, material);
			view->CreateStaticMeshInstance(geom, glm::translate(glm::mat4(1.0f), centre));
			spheres.push_back({ centre, i });
		}
	}

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	for (int i = 0; i < 4; ++i) gfx->DrawFrame(target, job);

	const assetlib::ImageData image = gfx->ScreenshotToMemory(target);
	REQUIRE(image.width == c_Width);
	REQUIRE(image.height == c_Height);

	const auto* pixels = reinterpret_cast<const uint8_t*>(image.pixels.data());
	for (const Sphere& sphere : spheres)
	{
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(sphere.centre, 1.0f);
		const auto      px   = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * c_Width);
		const auto      py   = static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * c_Height);
		REQUIRE(px < c_Width);
		REQUIRE(py < c_Height);

		const uint8_t*   rgba = pixels + (static_cast<size_t>(py) * c_Width + px) * 4;
		const glm::vec3  lit(rgba[0], rgba[1], rgba[2]);
		const glm::vec3& want = colours[sphere.surface];

		INFO(
			"surface " << sphere.surface << (sphere.centre.y > 0.0f ? " opaque" : " blend")
					   << " at (" << px << ", " << py << ") reads " << lit.r << ", " << lit.g
					   << ", " << lit.b);

		// Every channel the surface emits must stand clear of every one it does not.
		for (int litChannel = 0; litChannel < 3; ++litChannel)
		{
			for (int darkChannel = 0; darkChannel < 3; ++darkChannel)
			{
				if (want[litChannel] > 0.0f && want[darkChannel] == 0.0f)
				{
					CHECK(lit[litChannel] > lit[darkChannel] + 60.0f);
				}
			}
		}
	}

	std::filesystem::remove_all(dir);
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

	// The routing refusals: routes compose data slots alone, and a binding is whole or routed,
	// never both. The handle is fabricated and never dereferenced -- each refusal must fire
	// before the engine looks a route's texture up.
	const auto fake = TextureAssetHandle{ { 0, 1 }, 0 };

	SECTION("routes on a slot that binds whole")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(
				{ .surface  = "Rim",
		          .textures = { { .name = "baseColor", .routes = { { { fake, 0 } } } } } }),
			SceneError,
			MessageMatches(ContainsSubstring("routes compose data slots only")));
	}

	SECTION("a texture and routes at once")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(
				{ .surface  = "PbrLike",
		          .textures = { { .name    = "orm",
		                          .texture = fake,
		                          .routes  = { { { fake, 0 } } } } } }),
			SceneError,
			MessageMatches(ContainsSubstring("one or the other")));
	}

	SECTION("a route naming a channel a texture does not have")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial(
				{ .surface  = "PbrLike",
		          .textures = { { .name = "orm", .routes = { { { fake, 7 } } } } } }),
			SceneError,
			MessageMatches(ContainsSubstring("channels 0..3")));
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

	// Hashed alpha relates a UV footprint to texels, so it needs one of the surface's textures to
	// measure against. Tint declares none at all -- its coverage is arithmetic over a parameter --
	// so there is nothing to measure and the door that would create the material says so.
	SECTION("hashed alpha on a surface with nothing to measure coverage against")
	{
		CHECK_THROWS_MATCHES(
			scene->CreateSurfaceMaterial({ .surface = "Tint", .layerType = LayerType::kHashed }),
			SceneError,
			MessageMatches(ContainsSubstring("declares no coverage carrier")));
	}
}

// The other half of the refusal above, and what keeps it a statement about the carrier rather than
// about the layer: Rim declares a ColorSlot, which is a carrier, so the same layer it was refused
// for is accepted. Tint still draws every layer it always did -- the refusal is the hashed row's
// alone and not a surface being disqualified.
TEST_CASE("A surface declaring a carrier takes the hashed layer", "[surface][carrier][render]")
{
	auto gfx = bgl::CreateGraphics(SurfaceOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(SphereScene());
	REQUIRE(scene != nullptr);

	CHECK_NOTHROW(
		scene->CreateSurfaceMaterial({ .surface = "Rim", .layerType = LayerType::kHashed }));

	CHECK_NOTHROW(
		scene->CreateSurfaceMaterial({ .surface = "Tint", .layerType = LayerType::kMask }));
	CHECK_NOTHROW(
		scene->CreateSurfaceMaterial({ .surface = "Tint", .layerType = LayerType::kBlend }));
}

// The skinned tier's own gate, and the whole of what a tier costs a surface. The two tiers differ
// only in their geometry stage -- a pixel shader reads a ForwardVSOut and a material offset, and
// neither says which tier filled them -- so one quad, one surface material, drawn static and drawn
// skinned in its bind pose, has to land on the same pixels. A row derived wrong picks another slot's
// program or none at all, and that is what the equality catches. The frames after it are what stop
// the equality being two empty ones, and what separate a skinned draw from a static fallback.
//
// Linear rather than sectioned: a SECTION re-runs the body, and the body is a device.
TEST_CASE("A surface material draws on skinned geometry", "[surface][render][skinned]")
{
	using bgl::test::skinned_synth::AddQuadStaticGeom;
	using bgl::test::skinned_synth::AddSlidingQuadGeom;

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

	auto rim = scene->CreateSurfaceMaterial(
		{
			.surface = "Rim",
			.values  = { { "rimColor", glm::vec4(10.0f, 3.0f, 1.0f, 0.0f) },
	                     { "rimPower", glm::vec4(2.0f) },
	                     { "baseColorFactor", glm::vec4(0.05f, 0.05f, 0.06f, 1.0f) } },
		});

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = QuadCamera();
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	const auto* emptyPng   = "assets/golden/surface_skinned_empty.got.png";
	const auto* staticPng  = "assets/golden/surface_skinned_static_ref.got.png";
	const auto* skinnedPng = "assets/golden/surface_skinned_bind_pose.got.png";
	const auto* slidPng    = "assets/golden/surface_skinned_posed.got.png";
	const auto* blendPng   = "assets/golden/surface_skinned_blend.got.png";

	// What the environment alone draws, so every difference below has something to be measured
	// against rather than being trusted to be non-empty.
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, emptyPng);

	// The door this feature opens: before it, AddSkinnedMeshGeom threw on a game material.
	const auto skinned = AddSlidingQuadGeom(*scene, rim);
	REQUIRE(skinned.IsValid());
	CHECK(skinned.geomType == GeomType::kSkinnedMesh);

	const auto still = AddQuadStaticGeom(*scene, rim);
	REQUIRE(still.IsValid());

	const auto stillInstance = view->CreateStaticMeshInstance(still, glm::mat4(1.0f));
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, staticPng);
	view->DeleteMeshInstance(stillInstance);

	CHECK(bgl::test::FrameDelta(emptyPng, staticPng, 0, 0, 400, 300) > 1e-3f);

	// rate 0 holds frame 0, which slides by nothing, so the vertex the skinned geometry stage emits
	// is the one the static stage emits -- and the surface behind both is the one record.
	const auto posed = view->CreateSkinnedMeshInstance(skinned, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, skinnedPng);

	CHECK(bgl::test::FrameDelta(staticPng, skinnedPng, 0, 0, 400, 300) < 1e-6f);

	// The clamp clip at its end is the quad slid by a whole unit. If the skinned row had quietly
	// drawn through the static geometry stage the pose would not reach the vertices and this frame
	// would be the last one.
	view->SetSkinnedPlayback(
		posed,
		bgl::SkinnedPlaybackDesc::FromClip(bgl::test::skinned_synth::c_ClampClip, 1.0f, 0.0f));
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, slidPng);

	CHECK(bgl::test::FrameDelta(skinnedPng, slidPng, 0, 0, 400, 300) > 1e-3f);

	// The blended layer is one row both tiers share, so a skinned blended surface is drawn by the
	// pipeline the static one uses, off the depth-sorted list. Nothing else in the frame is blended,
	// so what this measures is that draw arriving at all.
	view->DeleteMeshInstance(posed);

	auto blend = scene->CreateSurfaceMaterial(
		{
			.surface     = "Rim",
			.layerType   = LayerType::kBlend,
			.doubleSided = false,
		});

	const auto blendedGeom = AddSlidingQuadGeom(*scene, blend);
	REQUIRE(blendedGeom.IsValid());

	view->CreateSkinnedMeshInstance(blendedGeom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, blendPng);

	CHECK(bgl::test::FrameDelta(emptyPng, blendPng, 0, 0, 400, 300) > 1e-3f);
}

namespace
{
	// A `size` x `size` single-mip RGBA8 image whose every texel is `rgba`.
	assetlib::ImageData
	FlatImage(uint32_t size, std::array<uint8_t, 4> rgba)
	{
		auto image     = assetlib::ImageData();
		image.width    = size;
		image.height   = size;
		image.vkFormat = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.pixels   = core::fixed_buffer<std::byte>(static_cast<size_t>(size) * size * 4);
		for (size_t t = 0; t < static_cast<size_t>(size) * size; ++t)
			for (size_t c = 0; c < 4; ++c)
				image.pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);
		image.subresources = { { 0, size * 4ull, static_cast<uint64_t>(size) * size * 4 } };
		return image;
	}
}

// The routing gate: a data slot composed from channel routes draws exactly what the same values
// bound whole draw. AO rides one source's R and roughness/metallic another's G and B -- the
// shape that motivated routing -- and both sources carry decoy values in their other channels,
// so a gather off the wrong channel or the wrong texture moves the frame.
TEST_CASE("A routed data slot draws what its composited map draws", "[surface][render]")
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

	const auto ao        = scene->AddTextureAsset(FlatImage(8, { { 200, 7, 9, 255 } }), "ao");
	const auto mr        = scene->AddTextureAsset(FlatImage(8, { { 3, 60, 90, 255 } }), "mr");
	const auto composite = scene->AddTextureAsset(FlatImage(8, { { 200, 60, 90, 255 } }), "whole");

	auto whole     = bgl::SurfaceMaterialDesc();
	whole.surface  = "PbrLike";
	whole.textures = { { .name = "orm", .texture = composite } };

	auto material = scene->CreateSurfaceMaterial(whole);
	auto sphere   = scene->AddSphereGeom(32, 32, 5.0f, material);
	view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = SphereCamera();
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	const auto shoot = [&](const char* path) {
		for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	shoot("assets/golden/surface_routed_whole.got.png");

	// The same channels as routes, landed as an update: the handle and the instance stand, only
	// the record's routes are rewritten.
	auto routed     = whole;
	routed.textures = { { .name = "orm", .routes = { { { ao, 0 }, { mr, 1 }, { mr, 2 } } } } };
	scene->UpdateSurfaceMaterial(material, routed);
	shoot("assets/golden/surface_routed_gather.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/surface_routed_whole.got.png",
			"assets/golden/surface_routed_gather.got.png"));

	// Rewired -- roughness now from ao's decoy G -- and its whole-bound equivalent, so the update
	// provably moved the routes and the moved routes still match their composite.
	auto rewired     = whole;
	rewired.textures = { { .name = "orm", .routes = { { { ao, 0 }, { ao, 1 }, { mr, 2 } } } } };
	scene->UpdateSurfaceMaterial(material, rewired);
	shoot("assets/golden/surface_rewired_gather.got.png");

	const auto rewiredWhole =
		scene->AddTextureAsset(FlatImage(8, { { 200, 7, 90, 255 } }), "rewired");
	auto back     = whole;
	back.textures = { { .name = "orm", .texture = rewiredWhole } };
	scene->UpdateSurfaceMaterial(material, back);
	shoot("assets/golden/surface_rewired_whole.got.png");

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/surface_rewired_whole.got.png",
			"assets/golden/surface_rewired_gather.got.png"));

	// MatchesGolden only deletes its `got` half; these were both gots.
	std::filesystem::remove("assets/golden/surface_routed_whole.got.png");
	std::filesystem::remove("assets/golden/surface_rewired_whole.got.png");
}

// The facing gate: on a double-sided material the reader's WorldNormal is the shaded face's
// normal, so a view-dependent surface term shades a back face exactly as it shades the front.
// The Rim fixture is that term -- through an unflipped interpolant a back face's
// 1 - saturate(dot(n, v)) saturates to 1 and the whole face blazes at full rimColor, which is
// how the defect looked on double-sided hair cards.
TEST_CASE("A double-sided surface shades its back face as its front", "[surface][render]")
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

	// The mask layer, because it is one whose pipeline draws both sides and leaves the cull to
	// the record -- the opaque row culls back faces in hardware, so a back face there is absent
	// rather than shaded. Coverage is pinned at 1 so every fragment survives the cutoff and the
	// two shots differ by shading alone. Rim well clear of the environment's own lighting, as the
	// side-by-side case sets it.
	auto material = scene->CreateSurfaceMaterial(
		{
			.surface     = "Rim",
			.layerType   = LayerType::kMask,
			.alphaCutoff = 0.5f,
			.doubleSided = true,
			.values      = { { "rimColor", glm::vec4(10.0f, 3.0f, 1.0f, 0.0f) },
	                         { "rimPower", glm::vec4(2.0f) },
	                         { "baseColorFactor", glm::vec4(0.05f, 0.05f, 0.06f, 1.0f) } },
		});

	auto       plane    = scene->AddPlaneGeom(4, 4, 10.0f, 7.0f, material);
	const auto instance = view->CreateStaticMeshInstance(plane, glm::mat4(1.0f));

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = SphereCamera();
	job.viewport = bgl::Viewport(400.0f, 300.0f);

	const auto shoot = [&](const char* path) {
		for (int i = 0; i < 6; ++i) gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	shoot("assets/golden/surface_facing_front.got.png");

	// The same plane about-faced: the silhouette is its own mirror image and nothing is
	// textured, so the frame may only change if facing does -- every pixel now shades through
	// the back-face path.
	view->SetInstanceTransform(
		instance,
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
	shoot("assets/golden/surface_facing_back.got.png");

	const bool facingMatches = bgl::test::MatchesGolden(
		"assets/golden/surface_facing_front.got.png",
		"assets/golden/surface_facing_back.got.png");
	CHECK(facingMatches);

	// MatchesGolden only deletes its `got` half; the front shot was a got too. On a failure both
	// stay on disk for inspection, as the matcher leaves its own got.
	if (facingMatches)
	{
		std::filesystem::remove("assets/golden/surface_facing_front.got.png");
	}
}
