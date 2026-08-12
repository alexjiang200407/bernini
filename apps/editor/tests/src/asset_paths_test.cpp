#include "util/asset_paths.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A texture is named by its suffix alone, whatever the case", "[thumbnails]")
{
	// The one rule the Content Explorer's tiles and the material graph's drop filter share.
	REQUIRE(editor::IsTextureFile("Textures/albedo.ktx2"));
	REQUIRE(editor::IsTextureFile("Textures/ALBEDO.KTX2"));

	REQUIRE_FALSE(editor::IsTextureFile("Meshes/apples.bmesh"));
	REQUIRE_FALSE(editor::IsTextureFile("Textures/albedo.png"));
	REQUIRE_FALSE(editor::IsTextureFile(""));
}
