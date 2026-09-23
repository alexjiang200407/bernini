#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BMaterial.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <assetlib/image_io.h>
#include <assetlib/texture_prune.h>

#include "MountAt.h"
#include "RefsSandbox.h"
#include "baked_name.h"
#include "mounted_io.h"
#include "texture_encoding.h"

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	// The `.<tag>-<8 hex>.ktx2` a content name resolves with.
	std::string
	SuffixOf(std::string_view contentName)
	{
		return bakedTextureKey(contentName).substr(contentName.size());
	}

}

TEST_CASE("a file key is drawn from as it is", "[bmaterial][encoding]")
{
	CHECK(bakedTextureKey("") == "");
	CHECK(
		bakedTextureKey("Derived/SourceTextures/albedo.ktx2") ==
		"Derived/SourceTextures/albedo.ktx2");
	CHECK(
		bakedTextureKey("Derived/BakedTextures/basecolor_0123456789abcdef.ktx2") ==
		"Derived/BakedTextures/basecolor_0123456789abcdef.ktx2");
}

TEST_CASE(
	"a content name is drawn from the file its group's encoding names",
	"[bmaterial][encoding]")
{
	constexpr std::string_view c_Hash = "_0123456789abcdef";

	const auto suffix = [&](std::string_view group) {
		return SuffixOf("Derived/BakedTextures/" + std::string(group) + std::string(c_Hash));
	};

	CHECK(suffix("basecolor").starts_with(".bc1-"));
	CHECK(suffix("basecoloralpha").starts_with(".bc7-"));
	CHECK(suffix("orm").starts_with(".bc7-"));
	CHECK(suffix("normal").starts_with(".bc5-"));
	CHECK(suffix("occlusion").starts_with(".bc4-"));
	CHECK(suffix("slot").starts_with(".bc7-"));

	// The suffix names the encoding and nothing else: one encoding, one suffix.
	CHECK(suffix("orm") == suffix("slot"));
	CHECK(suffix("orm").size() == std::string_view(".bc7-01234567.ktx2").size());
	CHECK(suffix("orm").ends_with(".ktx2"));

	const std::string key = bakedTextureKey("Derived/BakedTextures/normal_0123456789abcdef");
	CHECK(key.starts_with("Derived/BakedTextures/normal_0123456789abcdef.bc5-"));
}

TEST_CASE(
	"a reference that is neither a file nor a content name is refused",
	"[bmaterial][encoding]")
{
	CHECK_THROWS_AS(
		bakedTextureKey("Derived/BakedTextures/sky_0123456789abcdef"),
		std::runtime_error);
	CHECK_THROWS_AS(bakedTextureKey("Derived/BakedTextures/basecolor"), std::runtime_error);
	CHECK_THROWS_AS(bakedTextureKey("Derived/BakedTextures/basecolor_0123"), std::runtime_error);
}

TEST_CASE("one content under two encodings is one name and two files", "[bmaterial][encoding]")
{
	// The point of the split: re-encoding a map rewrites no document. Only the file moves.
	const std::string name = bakedMapContentName("basecolor", "some|content|key");

	const std::string bc1 = bakedMapEncodedName(name, textureEncoding(TextureRole::kBaseColor));
	const std::string bc7 =
		bakedMapEncodedName(name, textureEncoding(TextureRole::kBaseColorWithAlpha));

	CHECK(bc1 != bc7);
	CHECK(bc1.starts_with(name + ".bc1-"));
	CHECK(bc7.starts_with(name + ".bc7-"));
	CHECK(isBakedMapName(bc1));
	CHECK(isBakedMapName(bc7));

	// And the shape written before the halves were split is still recognised, so the prune can
	// sweep what it left behind.
	CHECK(isBakedMapName(name + ".ktx2"));
}

TEST_CASE("a bake records the content name and writes the encoded file", "[bmaterial][encoding]")
{
	const DataRoot root("bernini_split_name");
	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 90, 120, 200, 255 } });

	const BMaterial material = BakeAndSave(root, "m.bmaterial", "Derived/SourceTextures/a.ktx2");

	CHECK(material.pbr.baseColorTexture.starts_with("Derived/BakedTextures/basecolor_"));
	CHECK_FALSE(material.pbr.baseColorTexture.ends_with(".ktx2"));
	CHECK(fs::exists(root.path / bakedTextureKey(material.pbr.baseColorTexture)));
	CHECK_FALSE(fs::exists(root.path / (material.pbr.baseColorTexture + ".ktx2")));
}

TEST_CASE(
	"a material naming its map as a file re-bakes under a content name",
	"[bmaterial][encoding]")
{
	// What every document written before this holds. The map on disk is a file the bake can no
	// longer name, so the material is stale however fresh its sources are.
	const DataRoot root("bernini_legacy_name");
	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 30, 30, 30, 255 } });

	BMaterial material = BakeAndSave(root, "m.bmaterial", "Derived/SourceTextures/a.ktx2");

	const std::string legacy = material.pbr.baseColorTexture + ".ktx2";
	fs::rename(root.path / bakedTextureKey(material.pbr.baseColorTexture), root.path / legacy);
	material.pbr.baseColorTexture = legacy;

	REQUIRE(bakeIsStale(material, MountAt(root.path)));

	StoreAt(root.path).BakeMaterial(material);

	CHECK_FALSE(material.pbr.baseColorTexture.ends_with(".ktx2"));
	CHECK_FALSE(bakeIsStale(material, MountAt(root.path)));
}

TEST_CASE("a live map's file under another encoding is unused", "[bmaterial][encoding]")
{
	// A table change leaves the document alone and the old file behind: the sweep is what reclaims
	// it, and the map the current encoding names must survive it.
	const DataRoot root("bernini_stale_encoding");
	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 70, 10, 10, 255 } });

	const BMaterial material = BakeAndSave(root, "m.bmaterial", "Derived/SourceTextures/a.ktx2");

	const std::string live  = bakedTextureKey(material.pbr.baseColorTexture);
	const std::string other = bakedMapEncodedName(
		material.pbr.baseColorTexture,
		TextureEncoding{ "bc7", Ktx2Compression::kBC7_RGBA });
	fs::copy_file(root.path / live, root.path / other);

	const TexturePruneScan scan = AssetStore(root.path).FindUnusedBakedTextures();

	REQUIRE(scan.unused.size() == 1);
	CHECK(scan.unused.front().path == other);
}

TEST_CASE("a material recording content names reads through them", "[bmaterial][encoding]")
{
	const DataRoot root("bernini_baked_texture_key");
	WriteSource(root.path / "Derived/SourceTextures" / "a.ktx2", { { 200, 40, 10, 255 } });

	const BMaterial material = BakeAndSave(root, "m.bmaterial", "Derived/SourceTextures/a.ktx2");
	REQUIRE_FALSE(material.pbr.baseColorTexture.ends_with(".ktx2"));

	const std::string file = bakedTextureKey(material.pbr.baseColorTexture);

	SECTION("the bake is current while the resolved file is there")
	{
		CHECK_FALSE(bakeIsStale(material, MountAt(root.path)));

		fs::remove(root.path / file);
		CHECK(bakeIsStale(material, MountAt(root.path)));
	}

	SECTION("the reference graph holds the resolved file alive")
	{
		const AssetRefGraph graph = root.Scan();
		CHECK(graph.IsReferenced(file));
		CHECK_FALSE(graph.IsReferenced(material.pbr.baseColorTexture));
	}

	SECTION("moving the file keeps the content name when the suffix travels with it")
	{
		const std::string contentName = material.pbr.baseColorTexture;
		const std::string suffix      = file.substr(contentName.size());
		const std::string moved       = "Derived/BakedTextures/basecolor_fedcba9876543210" + suffix;

		REQUIRE(
			root.Source().RenameAsset(planRename(root.Scan(), file, moved)).status ==
			RenameStatus::kRenamed);

		const BMaterial after =
			StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial");
		CHECK(after.pbr.baseColorTexture == "Derived/BakedTextures/basecolor_fedcba9876543210");
		CHECK(bakedTextureKey(after.pbr.baseColorTexture) == moved);
	}

	SECTION("moving the file anywhere else names the file outright")
	{
		REQUIRE(
			root.Source()
				.RenameAsset(planRename(root.Scan(), file, "Derived/BakedTextures/kept.ktx2"))
				.status == RenameStatus::kRenamed);

		const BMaterial after =
			StoreAt(root.path).Load<BMaterial>("Authored/Materials/m.bmaterial");
		CHECK(after.pbr.baseColorTexture == "Derived/BakedTextures/kept.ktx2");
	}
}
