#include "StoreAt.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMaterial.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/RenderJob.h>
#include <bgl/glm.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <gamelib/AssetManager.h>
#include <string>

namespace
{
	// A game's shading function, in the project rather than in the engine's tree -- which is the
	// whole point: nothing here is staged by a build step, and the renderer finds it because the
	// project said where to look.
	constexpr const char* c_RimSource = R"(import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

struct RimParams
{
    [Default(0.2, 0.6, 1.0)]
    float3 rimColor;

    [Default(3.0)]
    float rimPower;

    [Default(0.05, 0.05, 0.06, 1.0)]
    float4 baseColorFactor;

    ColorSlot baseColor;
};

struct RimSurface : ISurfaceSource
{
    typealias MaterialParams = RimParams;

    static float Coverage<R : IMaterialReader>(R reader, RimParams params)
    {
        return params.baseColorFactor.a * reader.Sample(params.baseColor, reader.Uv()).a;
    }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, RimParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor =
            params.baseColorFactor * reader.Sample(params.baseColor, reader.Uv());

        let n = normalize(reader.WorldNormal());
        let v = normalize(reader.CameraPos() - reader.WorldPos());
        surface.emissive = params.rimColor * pow(1.0 - saturate(dot(n, v)), params.rimPower);

        return surface;
    }
};
)";

	// A scratch data root laid out like a project: the shaders under Authored/Shaders, where the
	// editor and a game both point the renderer.
	struct ProjectRoot
	{
		std::filesystem::path path;

		explicit ProjectRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path / assetlib::c_ShadersDirectoryName);

			std::ofstream(path / assetlib::c_ShadersDirectoryName / "Rim.slang") << c_RimSource;
		}
		~ProjectRoot() { std::filesystem::remove_all(path); }

		ProjectRoot(const ProjectRoot&) = delete;
		ProjectRoot&
		operator=(const ProjectRoot&) = delete;

		[[nodiscard]] std::filesystem::path
		Shaders() const
		{
			return path / assetlib::c_ShadersDirectoryName;
		}
	};

	// The document a person authors: a surface by name, and what it sets on it by name. Nothing
	// here is checked until the renderer has the surface in hand.
	// `rimColor` is well over one on purpose: the environment lights the sphere by itself, and an
	// emissive that only just clears that is one no frame comparison can see.
	void
	WriteRimMaterial(const std::filesystem::path& root, const char* file, glm::vec3 rimColor)
	{
		auto material           = assetlib::BMaterial();
		material.name           = "rim";
		material.shadingModel   = assetlib::ShadingModel::kSurface;
		material.surface.name   = "Rim";
		material.surface.values = { { "rimColor", { rimColor.r, rimColor.g, rimColor.b } },
			                        { "rimPower", { 2.0f } } };

		SaveAt(material, root / assetlib::c_MaterialsDirectoryName / file);
	}

	bgl::GraphicsOptions
	SurfaceOptions(const std::filesystem::path& shaderDir)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = false;
		opts.shaderCacheDir   = "shadercache";
		opts.surfaceShaderDir = shaderDir;
		return opts;
	}

	bgl::SceneDesc
	SurfaceSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 8;
		desc.initialSubmeshes            = 32;
		desc.initialMeshlets             = 512;
		desc.initialVertexBufferByteSize = 400000;
		desc.initialIndices              = 20000;
		desc.initialPbrMaterials         = 8;
		desc.initialSurfaceMaterials     = 8;
		return desc;
	}
}

// The seam this feature exists for, from the document down: a `.bmaterial` naming a surface the
// engine only learned about at startup, from a shader that lives in the project rather than in the
// engine's own tree.
TEST_CASE("A project's own surface draws the material that names it", "[gamelib][surface]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	ProjectRoot root("bernini_gamelib_surface");
	WriteRimMaterial(root.path, "warm.bmaterial", glm::vec3(10.0f, 3.0f, 1.0f));
	WriteRimMaterial(root.path, "cool.bmaterial", glm::vec3(1.0f, 3.0f, 10.0f));

	auto gfx = bgl::CreateGraphics(SurfaceOptions(root.Shaders()));
	REQUIRE(gfx != nullptr);

	// Read off the game's own module, so the name the document writes is the file's stem.
	REQUIRE(gfx->GetSurfaceTypes().size() == 1u);
	CHECK(gfx->GetSurfaceTypes()[0].name == "Rim");

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 256;
	targetDesc.height   = 256;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto scene = gfx->CreateScene(SurfaceSceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto assets = game::AssetManager(scene, root.path);

	const bgl::MaterialHandle warm = assets.AcquireMaterial("Authored/Materials/warm.bmaterial");
	REQUIRE(warm.IsValid());

	// A game row, not the PBR one: the document's shadingModel is what picked the branch, and the
	// kind came back off the surface the renderer registered.
	CHECK(warm.materialType == bgl::MaterialType::kGameStart);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(256.0f, 256.0f);

	const auto draw = [&](bgl::MaterialHandle material, const char* png) {
		const bgl::GeomHandle geom     = scene->AddSphereGeom(24, 24, 3.0f, material);
		const auto            instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

		for (int i = 0; i < 4; ++i) gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);

		view->DeleteMeshInstance(instance);
		scene->DeleteGeom(geom);
	};

	const auto* emptyPng = "assets/golden/gamelib_surface_empty.got.png";
	for (int i = 0; i < 4; ++i) gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, emptyPng);

	const auto* warmPng = "assets/golden/gamelib_surface_warm.got.png";
	draw(warm, warmPng);

	// Something was drawn, which is the half a handle alone cannot tell you: a material that
	// packed to zeros would still hand back a valid handle.
	CHECK(bgl::test::FrameDelta(emptyPng, warmPng, 0, 0, 256, 256) > 1e-3f);

	// And the document's parameters reached the record. The two materials differ in nothing but
	// `parameters`, so a reader that dropped them would draw the same sphere twice -- which is
	// exactly what the surface's own defaults would give.
	const auto* coolPng = "assets/golden/gamelib_surface_cool.got.png";
	draw(assets.AcquireMaterial("Authored/Materials/cool.bmaterial"), coolPng);

	CHECK(bgl::test::FrameDelta(warmPng, coolPng, 0, 0, 256, 256) > 1e-3f);
}

// The two setters that edit a PBR material in place. Neither has anything to say to a surface
// material -- the fields they write are PbrParams' and no surface reads one -- and the loose/baked
// pair they refuse each other with does not cover a third kind, so both have to say so themselves.
TEST_CASE("A surface material refuses the PBR setters", "[gamelib][surface]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	ProjectRoot root("bernini_gamelib_surface_setters");
	WriteRimMaterial(root.path, "warm.bmaterial", glm::vec3(10.0f, 3.0f, 1.0f));

	auto gfx = bgl::CreateGraphics(SurfaceOptions(root.Shaders()));
	REQUIRE(gfx != nullptr);

	auto scene  = gfx->CreateScene(SurfaceSceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	const bgl::MaterialHandle warm = assets.AcquireMaterial("Authored/Materials/warm.bmaterial");
	REQUIRE(warm.IsValid());

	// A surface material is neither loose nor baked, so the guard that tells those two apart lets
	// it through -- and the write would land in a field the record is never packed from, reporting
	// success and changing nothing on screen.
	CHECK_THROWS_MATCHES(
		assets.SetMaterialTexture(
			warm,
			game::AssetManager::TextureSlot::kBaseColor,
			"Derived/BakedTextures/other.ktx2"),
		bgl::SceneError,
		MessageMatches(ContainsSubstring("surface material")));

	// The loose setter already refuses it, for the reason it refuses a baked one.
	CHECK_THROWS_AS(
		assets.SetMaterialRoute(warm, 0, "Derived/SourceTextures/other.ktx2", 0),
		bgl::SceneError);
}

// The same document with the shader gone. A material names a surface the renderer never read, and
// there is nothing to draw it with -- so the failure has to say which surface, since the fix is to
// put that file back rather than to edit the material.
TEST_CASE("A material whose surface was never registered is refused", "[gamelib][surface]")
{
	using Catch::Matchers::ContainsSubstring;
	using Catch::Matchers::MessageMatches;

	ProjectRoot root("bernini_gamelib_surface_absent");
	WriteRimMaterial(root.path, "warm.bmaterial", glm::vec3(10.0f, 3.0f, 1.0f));

	// Everything the case above had, except the one line pointing at the project's shaders.
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = false;
	opts.shaderCacheDir   = "shadercache";

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);
	CHECK(gfx->GetSurfaceTypes().empty());

	auto scene  = gfx->CreateScene(SurfaceSceneDesc());
	auto assets = game::AssetManager(scene, root.path);

	CHECK_THROWS_MATCHES(
		assets.AcquireMaterial("Authored/Materials/warm.bmaterial"),
		bgl::SceneError,
		MessageMatches(ContainsSubstring("no surface named 'Rim' is registered")));
}
