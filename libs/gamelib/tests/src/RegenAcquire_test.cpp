#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <filesystem>
#include <gamelib/AssetManager.h>

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
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <bgl/IGraphics.h>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>

// The loads behind an acquire go through the regeneration seam: a stale container is served from
// its copied source, in memory, with the file on disk left exactly as it was -- #413's shape, where
// the fix used to be re-importing by hand.

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
