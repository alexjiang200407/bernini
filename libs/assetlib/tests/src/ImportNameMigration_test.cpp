#include "ImportUnitGroup.h"
#include "RefsSandbox.h"
#include "TexturedGltf.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace assetlib;
using namespace assetlib::test;

TEST_CASE("naming migration refuses outputs claimed by two sources", "[migrate][import-identity]")
{
	const DataRoot root("bernini-import-shared-output");
	const auto     store = root.Source();
	ImportUnitGroup(root.path, TexturedGltfPath());
	const auto key       = "Authored/Meshes/unit.bimport";
	auto       duplicate = store.Load<ImportDocument>(key);
	duplicate.source     = "Authored/Meshes/other.glb";
	store.Save(duplicate, "Authored/Meshes/other.bimport");
	const auto before = store.GetFiles().Read(duplicate.GetMeshOutput());
	const auto result = store.Migrate(false);
	CHECK(std::ranges::any_of(result.files, [](const auto& file) {
		return file.outcome == MigratedFile::Outcome::kFailed &&
		       file.message.contains("shared by multiple sources");
	}));
	CHECK(store.Load<ImportDocument>(key).identity.id == 0);
	CHECK(store.Load<ImportDocument>("Authored/Meshes/other.bimport").identity.id == 0);
	CHECK(store.GetFiles().Read(duplicate.GetMeshOutput()) == before);
}

TEST_CASE(
	"legacy import naming migration rewrites texture references and settles",
	"[migrate][import-identity]")
{
	const DataRoot root("bernini-import-name-migration");
	const auto     store       = root.Source();
	const auto     textureDir  = "Derived/SourceTextures/legacy";
	const auto     materialKey = "Authored/Materials/paint.bmaterial";
	ImportUnitGroup(root.path, TexturedGltfPath(), materialKey, c_DefaultSampleRate, textureDir);
	auto       material            = BMaterial();
	const auto texture             = store.GetFiles().Enumerate(textureDir).front();
	material.pbr.routes[0].texture = texture;
	store.Save(material, materialKey);
	const auto documentKey = "Authored/Meshes/unit.bimport";
	const auto before      = store.GetFiles().Read(documentKey);
	const auto dry         = store.Migrate(true);
	CHECK(dry.Count(MigratedFile::Outcome::kRewritten) > 0);
	CHECK(store.GetFiles().Read(documentKey) == before);
	CHECK(store.Exists("Derived/Meshes/unit.bmesh"));
	const auto report = store.Migrate(false);
	for (const auto& file : report.files) INFO(file.path.string() << ": " << file.message);
	REQUIRE(report.Count(MigratedFile::Outcome::kFailed) == 0);
	const auto document = store.Load<ImportDocument>(documentKey);
	CHECK(document.identity.id != 0);
	CHECK(document.identity.label == "unit.glb");
	CHECK(document.GetMeshOutput() == importOutputKey(document.identity, AssetType::kMesh));
	CHECK(store.Exists(document.GetMeshOutput()));
	CHECK_FALSE(store.Exists("Derived/Meshes/unit.bmesh"));
	CHECK(document.textureDir == importTextureDirectory(document.identity));
	const auto moved = store.Load<BMaterial>(materialKey).pbr.routes[0].texture;
	CHECK(moved.starts_with(document.textureDir + "/"));
	CHECK(store.Exists(moved));
	CHECK_FALSE(store.Exists(texture));
	const auto authored = store.GetFiles().Read(documentKey);
	const auto again    = store.Migrate(false);
	for (const auto& file : again.files)
	{
		INFO(file.path.string() << ": " << file.message);
		CHECK(file.outcome == MigratedFile::Outcome::kUnchanged);
	}
	CHECK(again.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(again.Count(MigratedFile::Outcome::kRewritten) == 0);
	CHECK(store.GetFiles().Read(documentKey) == authored);
}
