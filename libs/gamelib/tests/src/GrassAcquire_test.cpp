#include "ImportUnitGroup.h"
#include "PointsGltf.h"
#include "util/RigFixture.h"
#include "util/TestOptions.h"
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/BGrass.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/MaterialHandle.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <string>
#include <string_view>

// A mesh that grows grass brings it along when it is acquired: the geom holds every look its fields
// draw with, and each look holds its material, found through the mesh's own reference to the grass
// file rather than through anything that names the file after the mesh.

namespace
{
	namespace fs = std::filesystem;

	constexpr std::string_view c_MeshKey     = "Derived/Meshes/street.bmesh";
	constexpr std::string_view c_GrassKey    = "Derived/Meshes/street.bgrassfields";
	constexpr std::string_view c_LookKey     = "Authored/Grass/verge.bgrass";
	constexpr std::string_view c_MaterialKey = "Authored/Materials/green.bmaterial";

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	/** `street` imported, its POINTS field bound to a look whose material is `lookMaterial`. */
	void
	ImportStreet(const fs::path& dataRoot, std::string_view lookMaterial)
	{
		assetlib::test::Buffer    buffer;
		const assetlib::test::Glb glb(
			"bernini_grass_acquire.glb",
			assetlib::test::StreetDocument(buffer, assetlib::test::ShuffledGrid(8)),
			buffer.bytes);

		game::test::WriteTexture(dataRoot / "Textures/white.ktx2");
		game::test::WriteMaterial(dataRoot / c_MaterialKey, false);
		assetlib::test::ImportUnitGroup(dataRoot, glb.Path(), c_MaterialKey, 30.0f, {}, "street");

		const assetlib::AssetStore store(dataRoot);

		auto look     = assetlib::BGrass();
		look.material = std::string(lookMaterial);
		store.Save(look, std::string(c_LookKey));

		const std::string documentKey =
			assetlib::importDocumentKeyFor("Authored/Meshes/street.glb");
		auto document = store.Load<assetlib::ImportDocument>(documentKey);
		document.bindings.push_back({ .submesh = "Street[1]", .material = std::string(c_LookKey) });
		store.Save(document, documentKey);
	}

	/**
	 * How many references `material` holds once the street mesh is acquired, and once it is
	 * released again, over the one this test takes to be able to ask.
	 */
	struct Counts
	{
		uint32_t acquired = 0;
		uint32_t released = 0;
	};

	Counts
	CountAcquire(const fs::path& dataRoot)
	{
		auto gfx = bgl::CreateGraphics(HeadlessOptions());
		REQUIRE(gfx != nullptr);
		auto scene = gfx->CreateScene(bgl::SceneDesc());

		auto                      assets   = game::AssetManager(scene, dataRoot);
		const bgl::MaterialHandle material = assets.AcquireMaterial(c_MaterialKey);

		const bgl::GeomHandle geom = assets.AcquireMesh(c_MeshKey);
		REQUIRE(geom.IsValid());

		auto counts     = Counts();
		counts.acquired = assets.MaterialRefCount(material);
		assets.ReleaseGeom(geom);
		counts.released = assets.MaterialRefCount(material);
		return counts;
	}
}

TEST_CASE("An acquired mesh brings the grass it grows, and gives it back", "[grass][acquire]")
{
	const game::test::DataRoot root("bernini_grass_acquire");
	ImportStreet(root.path, c_MaterialKey);

	// Ours, the submesh's, and the look's.
	const Counts counts = CountAcquire(root.path);
	CHECK(counts.acquired == 3);
	CHECK(counts.released == 1);
}

TEST_CASE("A grass file renamed off its mesh's name is still the mesh's grass", "[grass][acquire]")
{
	const game::test::DataRoot root("bernini_grass_acquire_renamed");
	ImportStreet(root.path, c_MaterialKey);

	const assetlib::AssetStore store(root.path);
	const assetlib::RenamePlan plan = assetlib::planRename(
		assetlib::AssetRefGraph::Scan(store),
		c_GrassKey,
		"Derived/Meshes/verge.bgrassfields");
	REQUIRE(store.RenameAsset(plan).status == assetlib::RenameStatus::kRenamed);
	REQUIRE_FALSE(store.Exists(c_GrassKey));

	CHECK(CountAcquire(root.path).acquired == 3);
}

TEST_CASE("A look with no material leaves its field bare, and the mesh loads", "[grass][acquire]")
{
	const game::test::DataRoot root("bernini_grass_acquire_bare");
	ImportStreet(root.path, "");

	// Ours and the submesh's: the look took nothing.
	const Counts counts = CountAcquire(root.path);
	CHECK(counts.acquired == 2);
	CHECK(counts.released == 1);
}

// The patch a preview draws a look on holds that look the way a mesh does, and gives it back.
TEST_CASE("A grass patch holds its look until released", "[grass][acquire]")
{
	const game::test::DataRoot root("bernini_grass_patch");
	ImportStreet(root.path, c_MaterialKey);

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);
	auto scene = gfx->CreateScene(bgl::SceneDesc());

	auto                      assets   = game::AssetManager(scene, root.path);
	const bgl::MaterialHandle material = assets.AcquireMaterial(c_MaterialKey);

	const bgl::GeomHandle patch =
		assets.CreateGrassPatch({ .size = 4.0f, .spacing = 0.5f }, c_LookKey);
	REQUIRE(patch.IsValid());
	CHECK(assets.MaterialRefCount(material) == 2);

	assets.ReleaseGeom(patch);
	CHECK(assets.MaterialRefCount(material) == 1);
}

// An editor redraws a look as its author changes it, without saving it and without releasing what
// draws it. What reaches the screen is the renderer's to pin; these pin what the manager holds.
namespace
{
	constexpr std::string_view c_OtherMaterialKey = "Authored/Materials/red.bmaterial";

	/** A manager over `dataRoot` holding a reference on both materials, so their counts can be asked. */
	struct Held
	{
		explicit Held(const fs::path& dataRoot) :
			gfx(bgl::CreateGraphics(HeadlessOptions())), scene(gfx->CreateScene(bgl::SceneDesc())),
			assets(scene, dataRoot), green(assets.AcquireMaterial(c_MaterialKey)),
			red(assets.AcquireMaterial(c_OtherMaterialKey))
		{}

		bgl::GraphicsRef    gfx;
		bgl::SceneRef       scene;
		game::AssetManager  assets;
		bgl::MaterialHandle green;
		bgl::MaterialHandle red;
	};

	assetlib::BGrass
	LookWith(std::string_view material)
	{
		auto look            = assetlib::BGrass();
		look.material        = std::string(material);
		look.blade.maxHeight = 0.9f;
		return look;
	}
}

TEST_CASE("A look set while a patch holds it keeps its material", "[grass][set_look]")
{
	const game::test::DataRoot root("bernini_grass_set_look");
	ImportStreet(root.path, c_MaterialKey);
	game::test::WriteMaterial(root.path / c_OtherMaterialKey, false);
	Held held(root.path);

	const bgl::GeomHandle patch =
		held.assets.CreateGrassPatch({ .size = 4.0f, .spacing = 0.5f }, c_LookKey);
	REQUIRE(held.assets.MaterialRefCount(held.green) == 2);

	CHECK(held.assets.SetGrassLook(c_LookKey, LookWith(c_MaterialKey)));
	CHECK(held.assets.MaterialRefCount(held.green) == 2);

	// Not written: the store still holds the look as imported.
	const assetlib::AssetStore store(root.path);
	CHECK(store.Load<assetlib::BGrass>(std::string(c_LookKey)).blade.maxHeight != 0.9f);

	held.assets.ReleaseGeom(patch);
	CHECK(held.assets.MaterialRefCount(held.green) == 1);
}

TEST_CASE("A look set to another material moves its reference there", "[grass][set_look]")
{
	const game::test::DataRoot root("bernini_grass_set_material");
	ImportStreet(root.path, c_MaterialKey);
	game::test::WriteMaterial(root.path / c_OtherMaterialKey, false);
	Held held(root.path);

	const bgl::GeomHandle patch =
		held.assets.CreateGrassPatch({ .size = 4.0f, .spacing = 0.5f }, c_LookKey);

	REQUIRE(held.assets.SetGrassLook(c_LookKey, LookWith(c_OtherMaterialKey)));
	CHECK(held.assets.MaterialRefCount(held.green) == 1);
	CHECK(held.assets.MaterialRefCount(held.red) == 2);

	held.assets.ReleaseGeom(patch);
	CHECK(held.assets.MaterialRefCount(held.red) == 1);
}

TEST_CASE("A look nothing holds is not set", "[grass][set_look]")
{
	const game::test::DataRoot root("bernini_grass_set_unheld");
	ImportStreet(root.path, "");
	game::test::WriteMaterial(root.path / c_OtherMaterialKey, false);
	Held held(root.path);

	CHECK_FALSE(held.assets.SetGrassLook("Authored/Grass/nowhere.bgrass", LookWith(c_MaterialKey)));

	// Acquired bare, since the stored look names no material: there is nothing drawn to set.
	const bgl::GeomHandle patch =
		held.assets.CreateGrassPatch({ .size = 4.0f, .spacing = 0.5f }, c_LookKey);
	CHECK_FALSE(held.assets.SetGrassLook(c_LookKey, LookWith(c_MaterialKey)));
	CHECK(held.assets.MaterialRefCount(held.green) == 1);
	held.assets.ReleaseGeom(patch);
}

TEST_CASE("A look that cannot be drawn is refused and the old one stays", "[grass][set_look]")
{
	const game::test::DataRoot root("bernini_grass_set_refused");
	ImportStreet(root.path, c_MaterialKey);
	game::test::WriteMaterial(root.path / c_OtherMaterialKey, false);
	Held held(root.path);

	const bgl::GeomHandle patch =
		held.assets.CreateGrassPatch({ .size = 4.0f, .spacing = 0.5f }, c_LookKey);

	auto refused                 = LookWith(c_OtherMaterialKey);
	refused.clump.bladesPerClump = 0;
	CHECK_THROWS_AS(held.assets.SetGrassLook(c_LookKey, refused), bgl::SceneError);
	CHECK_THROWS(held.assets.SetGrassLook(c_LookKey, LookWith("")));
	CHECK_THROWS(
		held.assets.SetGrassLook(c_LookKey, LookWith("Authored/Materials/none.bmaterial")));

	CHECK(held.assets.MaterialRefCount(held.green) == 2);
	CHECK(held.assets.MaterialRefCount(held.red) == 1);

	// Still held and still settable.
	CHECK(held.assets.SetGrassLook(c_LookKey, LookWith(c_OtherMaterialKey)));
	held.assets.ReleaseGeom(patch);
}

TEST_CASE("A patch grows the look it is handed over the one the store holds", "[grass][set_look]")
{
	const game::test::DataRoot root("bernini_grass_patch_authored");
	ImportStreet(root.path, "");
	game::test::WriteMaterial(root.path / c_OtherMaterialKey, false);
	Held held(root.path);

	const bgl::GeomHandle patch = held.assets.CreateGrassPatch(
		{ .size = 4.0f, .spacing = 0.5f },
		c_LookKey,
		LookWith(c_MaterialKey));
	CHECK(held.assets.MaterialRefCount(held.green) == 2);
	CHECK(held.assets.SetGrassLook(c_LookKey, LookWith(c_OtherMaterialKey)));

	held.assets.ReleaseGeom(patch);
	CHECK(held.assets.MaterialRefCount(held.green) == 1);
	CHECK(held.assets.MaterialRefCount(held.red) == 1);
}
