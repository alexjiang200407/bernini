#include <gamelib/AssetManager.h>
#include <gamelib/vat_freshness.h>

#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "SkinnedGltf.h"
#include "util/RigFixture.h"
#include "util/TestOptions.h"

#include "StoreAt.h"
#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <bgl/IGraphics.h>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>

// The loads behind an acquire go through the regeneration seam: a stale container is served from
// its copied source, in memory, with the file on disk left exactly as it was -- #413's shape, where
// the fix used to be re-importing by hand. The VAT rule rides the same axis: a bake over a stale
// group is stale whatever its own stamps say.

namespace
{
	using game::test::DataRoot;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		return opts;
	}

	/** One imported source group, with the real material the skinned pipeline requires. */
	void
	ImportRig(const std::filesystem::path& dataRoot)
	{
		const assetlib::test::SkinnedGltf source("bernini_regen_acquire_gltf");

		game::test::WriteTexture(dataRoot / "Textures/white.ktx2");
		game::test::WriteMaterial(dataRoot / "Authored/Materials/unit.bmaterial", false);
		assetlib::test::ImportUnitGroup(
			dataRoot,
			source.PackGlb(),
			"Authored/Materials/unit.bmaterial");
	}

	void
	FlipTokenByte(const std::filesystem::path& path)
	{
		assetlib::test::TamperHeaderByte(path, assetlib::test::c_TokenOffset);
	}
}

TEST_CASE("a stale clip set is refused at acquire, and names the way out", "[regen][acquire]")
{
	DataRoot root("bernini_regen_acquire");
	ImportRig(root.path);

	const std::filesystem::path banim = root.path / "Derived/Animations/unit.banim";

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto scene = gfx->CreateScene(bgl::SceneDesc());
	auto view  = gfx->CreateSceneView(scene, 8);

	SECTION("current, it acquires")
	{
		auto       assets = game::AssetManager(scene, root.path);
		const auto skinned =
			assets.AcquireSkinnedMesh("Derived/Meshes/unit.bmesh", "Derived/Animations/unit.banim");
		CHECK(skinned.geom.IsValid());
		REQUIRE(skinned.clips.size() == 2);
		CHECK(skinned.clips[0].name == "walk");
	}

	SECTION("stale, it refuses rather than paying an import on every load")
	{
		FlipTokenByte(banim);
		const auto stale = core::file::read_file_bytes(banim.string());

		auto assets = game::AssetManager(scene, root.path);
		CHECK_THROWS_WITH(
			assets.AcquireSkinnedMesh("Derived/Meshes/unit.bmesh", "Derived/Animations/unit.banim"),
			Catch::Matchers::ContainsSubstring("assetlib_cli migrate"));

		// Refused, not repaired: making the file current is migrate's, and a load that wrote one
		// would be a load that writes.
		CHECK(core::file::read_file_bytes(banim.string()) == stale);
	}
}

TEST_CASE(
	"a .bvat over a stale group is stale, and re-bakes from the regenerated geometry",
	"[regen][vat]")
{
	DataRoot root("bernini_regen_vat");
	ImportRig(root.path);

	const assetlib::AssetStore store(root.path);
	const std::string_view     mesh  = "Derived/Meshes/unit.bmesh";
	const std::string_view     clips = "Derived/Animations/unit.banim";

	static_cast<void>(game::EnsureVatBaked(store, mesh, clips));
	REQUIRE(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kFresh);

	// The bake's own three stamps still hold -- the staleness is the group's cache key alone.
	FlipTokenByte(root.path / "Derived/Meshes/unit.bmesh");
	CHECK(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kStale);

	// Re-baked from the seam's outputs, not refused -- and the answer the bake just made true
	// holds: without that, the editor's bake offer would loop and every acquire would pay the
	// bake again until migrate rewrites the group on disk.
	const assetlib::BVat rebaked = game::EnsureVatBaked(store, mesh, clips);
	CHECK_FALSE(rebaked.positionsKtx2.empty());
	CHECK(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kFresh);

	// That trust is pinned to the group's source, not just to the file: a re-exported .glb
	// moves nothing the bake's own stamps watch, and must still fire the axis.
	const assetlib::test::SkinnedGltf reexport(
		"bernini_regen_vat_reexport_gltf",
		{ { "\"translation\": [ 0, 2, 0 ]", "\"translation\": [ 0, 3, 0 ]" } });
	std::filesystem::copy_file(
		reexport.PackGlb(),
		root.path / "Authored/Meshes/unit.glb",
		std::filesystem::copy_options::overwrite_existing);
	CHECK(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kStale);

	// And a re-bake over the re-export earns it back.
	static_cast<void>(game::EnsureVatBaked(store, mesh, clips));
	CHECK(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kFresh);

	// A bake this process did not make earns no such trust. Simulated by moving the file's
	// write time -- how a sibling's bake actually arrives, stamped by the checkout that wrote
	// it -- since rewriting identical bytes can land inside the same stamp second.
	const std::filesystem::path bvatAbs =
		root.path /
		assetlib::vatPathFor("Derived/Meshes/unit.bmesh", "Derived/Animations/unit.banim");
	std::filesystem::last_write_time(
		bvatAbs,
		std::filesystem::last_write_time(bvatAbs) + std::chrono::seconds(2));
	CHECK(game::VatFreshness(store, mesh, clips) == game::VatBakeState::kStale);
}
