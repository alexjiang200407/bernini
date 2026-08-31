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

TEST_CASE("A key under a root is the one answer to is this inside that", "[assetpaths]")
{
	const QString root = QStringLiteral("/projects/MyGame/Data");

	CHECK(
		editor::GetKeyUnder(root, root + "/Authored/Meshes/kirk.glb") ==
		QString("Authored/Meshes/kirk.glb"));

	// A directory contains itself, and a caller that cares can tell "." from empty.
	CHECK(editor::GetKeyUnder(root, root) == QString("."));

	// Cleaned first, so a key is judged on where it lands rather than how it is spelt.
	CHECK(
		editor::GetKeyUnder(root, root + "/Authored/../Derived/Meshes/kirk.bmesh") ==
		QString("Derived/Meshes/kirk.bmesh"));

	SECTION("outside is empty, however it is spelt")
	{
		CHECK(editor::GetKeyUnder(root, "/projects/MyGame/Other/kirk.glb").isEmpty());
		CHECK(editor::GetKeyUnder(root, "/projects/MyGame").isEmpty());
		CHECK(editor::GetKeyUnder(root, "/elsewhere/kirk.glb").isEmpty());
		CHECK(editor::GetKeyUnder(root, root + "/../sneaky.glb").isEmpty());
		CHECK(editor::GetKeyUnder(root, {}).isEmpty());
		CHECK(editor::GetKeyUnder({}, root + "/Authored").isEmpty());
	}

	SECTION("a name that only a typed path would be refused for is still inside")
	{
		// Why this is not IsContainedRelativePath: that one refuses `:` because a *typed* relative
		// path spelling a drive would re-root the join it is about to go into. relativeFilePath
		// cannot return such a thing, and one caller here gates every deletion -- reading a held
		// file as outside its own folder is how a deletion goes through while a panel has it open.
		CHECK(
			editor::GetKeyUnder(root, root + "/Authored/Materials/a:b.bmaterial") ==
			QString("Authored/Materials/a:b.bmaterial"));

		// And a folder whose name merely begins with dots is a folder, not a climb.
		CHECK(
			editor::GetKeyUnder(root, root + "/Authored/..hidden/x.bmaterial") ==
			QString("Authored/..hidden/x.bmaterial"));
	}
}

TEST_CASE("The predicate spelling answers exactly what the key does", "[assetpaths]")
{
	const QString root = QStringLiteral("/projects/MyGame/Data");

	CHECK(editor::IsKeyUnder(root, root + "/Authored/Meshes/kirk.glb"));

	// The root itself is inside itself -- "." is a key, not a refusal, and a predicate that
	// disagreed with the key would be the divergence this whole seam exists to remove.
	CHECK(editor::IsKeyUnder(root, root));

	CHECK_FALSE(editor::IsKeyUnder(root, "/elsewhere/kirk.glb"));
	CHECK_FALSE(editor::IsKeyUnder(root, root + "/../sneaky.glb"));
	CHECK_FALSE(editor::IsKeyUnder(root, {}));
	CHECK_FALSE(editor::IsKeyUnder({}, root + "/Authored"));
}
