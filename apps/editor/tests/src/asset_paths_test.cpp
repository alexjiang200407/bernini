#include "util/asset_paths.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A texture is named by its suffix alone, whatever the case", "[thumbnails]")
{
	// The one rule the Content Explorer's tiles and the material graph's drop filter share.
	REQUIRE(editor::IsTextureFile("Textures/albedo.ktx2"));
	REQUIRE(editor::IsTextureFile("Textures/ALBEDO.KTX2"));

	REQUIRE_FALSE(editor::IsTextureFile("Derived/Meshes/apples.bmesh"));
	REQUIRE_FALSE(editor::IsTextureFile("Textures/albedo.png"));
	REQUIRE_FALSE(editor::IsTextureFile(""));
}

TEST_CASE("The explorer lists neither a build product nor a source's sidecar", "[assetpaths]")
{
	// Two members, two reasons. A `.bvat` is re-bakeable from its inputs and never committed, so
	// offering it for rename or delete implies an authorship it does not have. A `.bimport` is the
	// settings sidecar of the `.glb` beside it, and the source is the row that stands for the model.
	CHECK(editor::IsHiddenInExplorer("Derived/Meshes/unit.bvat"));
	CHECK(editor::IsHiddenInExplorer("Derived/Meshes/UNIT.BVAT"));
	CHECK(editor::IsHiddenInExplorer("Authored/Meshes/kirk.bimport"));
	CHECK(editor::IsHiddenInExplorer("Authored/Meshes/KIRK.BIMPORT"));

	// The source itself is the row, and everything else is listed as it always was.
	CHECK_FALSE(editor::IsHiddenInExplorer("Authored/Meshes/kirk.glb"));
	CHECK_FALSE(editor::IsHiddenInExplorer("Derived/Meshes/kirk.bmesh"));
	CHECK_FALSE(editor::IsHiddenInExplorer("Authored/Materials/skin.bmaterial"));
	CHECK_FALSE(editor::IsHiddenInExplorer({}));
}
