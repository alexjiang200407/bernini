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
