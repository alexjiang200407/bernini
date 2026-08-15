#include <assetlib/AssetStore.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>
#include <core/file/LooseFileSystem.h>

#include "bmesh_texture.h"

using namespace assetlib;

namespace
{
	// A scratch data root that cleans up after itself.
	struct DataRoot
	{
		std::filesystem::path path;

		explicit DataRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path / "Materials");
		}
		~DataRoot() { std::filesystem::remove_all(path); }

		std::filesystem::path
		Textures() const
		{
			return path / "Textures";
		}
	};

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	void
	WriteSource(const std::filesystem::path& path, uint32_t size, std::array<uint8_t, 4> rgba)
	{
		std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4);
		for (size_t t = 0; t < static_cast<size_t>(size) * size; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}

	// Bakes a material whose base colour reads `source`, and saves it as `<root>/Materials/<name>`.
	BMaterial
	BakeAndSave(const DataRoot& root, const char* name, const char* store)
	{
		BMaterial material;
		material.pbr.routes[0] = { store, 0 };

		bakeMaterial(material, MaterialBakeDesc{ root.path });
		saveMaterial(material, root.path / "Materials" / name);
		return material;
	}

	size_t
	CountMaps(const std::filesystem::path& dir)
	{
		size_t count = 0;
		for (const auto& entry : std::filesystem::directory_iterator(dir))
			if (isBakedMapName(entry.path().filename().string()))
				++count;
		return count;
	}
}

TEST_CASE("isBakedMapName recognizes only what the bake writes", "[texture_prune]")
{
	// The bake's own naming: <group>_<16 hex>.ktx2.
	CHECK(isBakedMapName("basecolor_700a22db7b7ef785.ktx2"));
	CHECK(isBakedMapName("orm_fdc537ad982f59e7.ktx2"));
	CHECK(isBakedMapName("normal_3fd6ecf5f0d1476c.ktx2"));

	// A texture the material bake did not write. Nothing references one of these from a material, so
	// the name is the only thing standing between it and the sweep.
	CHECK_FALSE(isBakedMapName("skybox.ktx2"));
	CHECK_FALSE(isBakedMapName("iem.ktx2"));
	CHECK_FALSE(isBakedMapName("pmrem.ktx2"));

	SECTION("a near-miss is not a match")
	{
		CHECK_FALSE(isBakedMapName("basecolor_700a22db7b7ef785.png"));    // not a .ktx2
		CHECK_FALSE(isBakedMapName("basecolor_700a22db7b7ef78.ktx2"));    // 15 digits
		CHECK_FALSE(isBakedMapName("basecolor_700a22db7b7ef7851.ktx2"));  // 17 digits
		CHECK_FALSE(isBakedMapName("basecolor_700a22db7b7ef78z.ktx2"));   // not hex
		CHECK_FALSE(isBakedMapName("albedo_700a22db7b7ef785.ktx2"));  // not a group the bake writes
		CHECK_FALSE(isBakedMapName("basecolor.ktx2"));                // no hash at all
	}
}

TEST_CASE("findUnusedBakedTextures finds the map a re-bake orphaned", "[texture_prune]")
{
	// The whole reason these accumulate: the file name is a hash of the routing, so re-routing a
	// material writes a *new* file and simply stops naming the old one.
	const DataRoot root("bernini_prune_orphan");

	WriteSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	WriteSource(root.path / "b.ktx2", 16, { { 0, 200, 0, 255 } });

	const BMaterial first  = BakeAndSave(root, "mat.bmaterial", "a.ktx2");
	const BMaterial second = BakeAndSave(root, "mat.bmaterial", "b.ktx2");

	REQUIRE(first.pbr.baseColorTexture != second.pbr.baseColorTexture);
	REQUIRE(CountMaps(root.Textures()) == 2);

	const auto scan = findUnusedBakedTextures(AssetStore(root.path));

	SECTION("the abandoned map is reported, and only it")
	{
		REQUIRE(scan.unused.size() == 1);
		CHECK(scan.unused.front().path == first.pbr.baseColorTexture);
		CHECK(scan.unused.front().bytes > 0);
		CHECK(scan.bytes == scan.unused.front().bytes);
	}

	SECTION("the scan counts what it walked")
	{
		CHECK(scan.materialsScanned == 1);
		CHECK(scan.liveMaps == 1);
		CHECK(scan.candidates == 2);
	}

	SECTION("deleting removes exactly the orphan")
	{
		const auto result = deleteUnusedBakedTextures(scan, AssetStore(root.path));

		CHECK(result.deleted == 1);
		CHECK(result.bytes == scan.bytes);
		CHECK(result.failed.empty());

		CHECK_FALSE(std::filesystem::exists(root.path / first.pbr.baseColorTexture));
		CHECK(std::filesystem::exists(root.path / second.pbr.baseColorTexture));
		CHECK(CountMaps(root.Textures()) == 1);
	}

	SECTION("a second scan finds nothing left to do")
	{
		deleteUnusedBakedTextures(scan, AssetStore(root.path));

		const auto again = findUnusedBakedTextures(AssetStore(root.path));
		CHECK(again.unused.empty());
		CHECK(again.candidates == 1);
	}
}

TEST_CASE("findUnusedBakedTextures keeps a map another material still shares", "[texture_prune]")
{
	// Baked maps are shared, not owned: two materials routing a group identically converge on one file.
	// Pruning "the maps this material no longer names" would delete it out from under the other one.
	const DataRoot root("bernini_prune_shared");

	WriteSource(root.path / "shared.ktx2", 16, { { 10, 60, 90, 255 } });
	WriteSource(root.path / "other.ktx2", 16, { { 90, 60, 10, 255 } });

	const BMaterial keeper = BakeAndSave(root, "keeper.bmaterial", "shared.ktx2");

	// A second material bakes the same map, then re-bakes onto a different source and drops it.
	BMaterial rebaked = BakeAndSave(root, "rebaked.bmaterial", "shared.ktx2");
	REQUIRE(rebaked.pbr.baseColorTexture == keeper.pbr.baseColorTexture);

	rebaked = BakeAndSave(root, "rebaked.bmaterial", "other.ktx2");
	REQUIRE(rebaked.pbr.baseColorTexture != keeper.pbr.baseColorTexture);

	const auto scan = findUnusedBakedTextures(AssetStore(root.path));

	// The shared map is still named by `keeper`, so it is live even though `rebaked` walked away.
	CHECK(scan.materialsScanned == 2);
	CHECK(scan.unused.empty());

	deleteUnusedBakedTextures(scan, AssetStore(root.path));
	CHECK(std::filesystem::exists(root.path / keeper.pbr.baseColorTexture));
}

TEST_CASE("findUnusedBakedTextures keeps a stale material's baked triplet", "[texture_prune]")
{
	// A material whose bake has gone stale renders from its routes, but it still carries the triplet
	// that bake wrote, and re-stamping must not find the maps gone. Drawing from the routes today is
	// not a claim that the triplet is dead.
	const DataRoot root("bernini_prune_loose");

	WriteSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });

	BMaterial material = BakeAndSave(root, "loose.bmaterial", "a.ktx2");

	// Rewind a stamp so the bake reads stale without touching the maps it wrote.
	material.pbr.routeStamps[0] = SourceStamp{ 1, 1 };
	saveMaterial(material, root.path / "Materials" / "loose.bmaterial");

	const auto scan = findUnusedBakedTextures(AssetStore(root.path));

	CHECK(scan.liveMaps == 1);
	CHECK(scan.unused.empty());
}

TEST_CASE("findUnusedBakedTextures never sweeps a hand-placed map", "[texture_prune]")
{
	// A texture that shares the directory but that no material names. Nothing marks it, so only its
	// name keeps it alive.
	const DataRoot root("bernini_prune_handplaced");

	WriteSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	BakeAndSave(root, "mat.bmaterial", "a.ktx2");

	WriteSource(root.Textures() / "skybox.ktx2", 8, { { 1, 2, 3, 255 } });
	WriteSource(root.Textures() / "logo.ktx2", 8, { { 4, 5, 6, 255 } });

	const auto scan = findUnusedBakedTextures(AssetStore(root.path));

	CHECK(scan.candidates == 1);  // the baked base colour, and nothing else
	CHECK(scan.unused.empty());

	deleteUnusedBakedTextures(scan, AssetStore(root.path));

	CHECK(std::filesystem::exists(root.Textures() / "skybox.ktx2"));
	CHECK(std::filesystem::exists(root.Textures() / "logo.ktx2"));
}

TEST_CASE("findUnusedBakedTextures refuses to run on an unreadable material", "[texture_prune]")
{
	// The fail-safe. A material we cannot parse is a material whose maps we cannot mark -- proceeding
	// would sweep them as garbage, so the scan aborts instead and nothing is deleted.
	const DataRoot root("bernini_prune_corrupt");

	WriteSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	const BMaterial material = BakeAndSave(root, "good.bmaterial", "a.ktx2");

	std::ofstream(root.path / "Materials" / "broken.bmaterial", std::ios::binary)
		<< "not a material";

	REQUIRE_THROWS_AS(findUnusedBakedTextures(AssetStore(root.path)), std::runtime_error);
	CHECK(std::filesystem::exists(root.path / material.pbr.baseColorTexture));
}

TEST_CASE("findUnusedBakedTextures handles a project with nothing baked", "[texture_prune]")
{
	const DataRoot root("bernini_prune_empty");

	SECTION("no texture directory is not an error: nothing baked, nothing orphaned")
	{
		const auto scan = findUnusedBakedTextures(AssetStore(root.path));

		CHECK(scan.unused.empty());
		CHECK(scan.candidates == 0);
		CHECK(scan.materialsScanned == 0);
	}

	SECTION("a data root that does not exist is a caller error")
	{
		REQUIRE_THROWS_AS(AssetStore(root.path / "nope"), std::runtime_error);
	}

	// A source built over a mount cannot check its writable layer -- an overlay's may legitimately
	// not exist yet -- so the sweep, which unlinks files there, checks for itself. Without it a
	// stale writable root reports a clean project rather than the caller error it is.
	SECTION("nor when the source was built over a mount, which cannot check for it")
	{
		const AssetStore store(
			root.path / "nope",
			std::make_shared<core::file::LooseFileSystem>(root.path));

		REQUIRE_THROWS_AS(findUnusedBakedTextures(store), std::runtime_error);
	}
}

TEST_CASE("findUnusedBakedTextures honours a custom texture directory", "[texture_prune]")
{
	const DataRoot root("bernini_prune_texdir");

	WriteSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	WriteSource(root.path / "b.ktx2", 16, { { 0, 200, 0, 255 } });

	auto desc       = TexturePruneDesc();
	desc.textureDir = "cooked";

	auto bake       = MaterialBakeDesc{ root.path };
	bake.textureDir = "cooked";

	BMaterial material;
	material.pbr.routes[0] = { "a.ktx2", 0 };
	bakeMaterial(material, bake);
	const std::string orphan = material.pbr.baseColorTexture;

	material.pbr.routes[0] = { "b.ktx2", 0 };
	bakeMaterial(material, bake);
	saveMaterial(material, root.path / "Materials" / "mat.bmaterial");

	const auto scan = findUnusedBakedTextures(AssetStore(root.path), desc);

	REQUIRE(scan.unused.size() == 1);
	CHECK(scan.unused.front().path == orphan);
	CHECK(orphan.starts_with("cooked/"));

	CHECK(deleteUnusedBakedTextures(scan, AssetStore(root.path)).deleted == 1);
	CHECK_FALSE(std::filesystem::exists(root.path / orphan));
}
