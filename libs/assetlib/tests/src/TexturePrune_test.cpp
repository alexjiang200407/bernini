#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/texture_prune.h>

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
		textures() const
		{
			return path / "Textures";
		}
	};

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 whose every texel is `rgba`.
	void
	writeSource(const std::filesystem::path& path, uint32_t size, std::array<uint8_t, 4> rgba)
	{
		std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4);
		for (size_t t = 0; t < static_cast<size_t>(size) * size; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}

	// Bakes a material whose base colour reads `source`, and saves it as `<root>/Materials/<name>`.
	BMaterial
	bakeAndSave(const DataRoot& root, const char* name, const char* source)
	{
		BMaterial material;
		material.routes[0] = { source, 0 };

		bakeMaterial(material, MaterialBakeDesc{ root.path });
		saveMaterial(material, root.path / "Materials" / name);
		return material;
	}

	size_t
	countMaps(const std::filesystem::path& dir)
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

	// The maps the repo keeps loose in the same directory. Deleting one of these would break the
	// renderer, and nothing references them from a material -- so the name is the only thing standing
	// between them and the sweep.
	CHECK_FALSE(isBakedMapName("skybox.ktx2"));
	CHECK_FALSE(isBakedMapName("iem.ktx2"));
	CHECK_FALSE(isBakedMapName("pmrem.ktx2"));
	CHECK_FALSE(isBakedMapName("brdf_lut.ktx2"));

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

	writeSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	writeSource(root.path / "b.ktx2", 16, { { 0, 200, 0, 255 } });

	const BMaterial first  = bakeAndSave(root, "mat.bmaterial", "a.ktx2");
	const BMaterial second = bakeAndSave(root, "mat.bmaterial", "b.ktx2");

	REQUIRE(first.baseColorTexture != second.baseColorTexture);
	REQUIRE(countMaps(root.textures()) == 2);

	const auto scan = findUnusedBakedTextures(TexturePruneDesc{ root.path });

	SECTION("the abandoned map is reported, and only it")
	{
		REQUIRE(scan.unused.size() == 1);
		CHECK(scan.unused.front().path == first.baseColorTexture);
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
		const auto result = deleteUnusedBakedTextures(scan, TexturePruneDesc{ root.path });

		CHECK(result.deleted == 1);
		CHECK(result.bytes == scan.bytes);
		CHECK(result.failed.empty());

		CHECK_FALSE(std::filesystem::exists(root.path / first.baseColorTexture));
		CHECK(std::filesystem::exists(root.path / second.baseColorTexture));
		CHECK(countMaps(root.textures()) == 1);
	}

	SECTION("a second scan finds nothing left to do")
	{
		deleteUnusedBakedTextures(scan, TexturePruneDesc{ root.path });

		const auto again = findUnusedBakedTextures(TexturePruneDesc{ root.path });
		CHECK(again.unused.empty());
		CHECK(again.candidates == 1);
	}
}

TEST_CASE("findUnusedBakedTextures keeps a map another material still shares", "[texture_prune]")
{
	// Baked maps are shared, not owned: two materials routing a group identically converge on one file.
	// Pruning "the maps this material no longer names" would delete it out from under the other one.
	const DataRoot root("bernini_prune_shared");

	writeSource(root.path / "shared.ktx2", 16, { { 10, 60, 90, 255 } });
	writeSource(root.path / "other.ktx2", 16, { { 90, 60, 10, 255 } });

	const BMaterial keeper = bakeAndSave(root, "keeper.bmaterial", "shared.ktx2");

	// A second material bakes the same map, then re-bakes onto a different source and drops it.
	BMaterial rebaked = bakeAndSave(root, "rebaked.bmaterial", "shared.ktx2");
	REQUIRE(rebaked.baseColorTexture == keeper.baseColorTexture);

	rebaked = bakeAndSave(root, "rebaked.bmaterial", "other.ktx2");
	REQUIRE(rebaked.baseColorTexture != keeper.baseColorTexture);

	const auto scan = findUnusedBakedTextures(TexturePruneDesc{ root.path });

	// The shared map is still named by `keeper`, so it is live even though `rebaked` walked away.
	CHECK(scan.materialsScanned == 2);
	CHECK(scan.unused.empty());

	deleteUnusedBakedTextures(scan, TexturePruneDesc{ root.path });
	CHECK(std::filesystem::exists(root.path / keeper.baseColorTexture));
}

TEST_CASE("findUnusedBakedTextures keeps a loose material's baked triplet", "[texture_prune]")
{
	// A kLoose material renders from its routes, but it still carries the triplet its last bake wrote,
	// and switching it back to kBaked must not find the maps gone. `mode` says what the renderer draws
	// from -- it is not a claim that the triplet is dead.
	const DataRoot root("bernini_prune_loose");

	writeSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });

	BMaterial material = bakeAndSave(root, "loose.bmaterial", "a.ktx2");

	material.mode = MaterialMode::kLoose;
	saveMaterial(material, root.path / "Materials" / "loose.bmaterial");

	const auto scan = findUnusedBakedTextures(TexturePruneDesc{ root.path });

	CHECK(scan.liveMaps == 1);
	CHECK(scan.unused.empty());
}

TEST_CASE("findUnusedBakedTextures never sweeps a hand-placed map", "[texture_prune]")
{
	// The IBL set and the skybox live in the same directory and are named in config, not in any
	// material. Nothing marks them, so only their names keep them alive.
	const DataRoot root("bernini_prune_handplaced");

	writeSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	bakeAndSave(root, "mat.bmaterial", "a.ktx2");

	writeSource(root.textures() / "skybox.ktx2", 8, { { 1, 2, 3, 255 } });
	writeSource(root.textures() / "brdf_lut.ktx2", 8, { { 4, 5, 6, 255 } });

	const auto scan = findUnusedBakedTextures(TexturePruneDesc{ root.path });

	CHECK(scan.candidates == 1);  // the baked base colour, and nothing else
	CHECK(scan.unused.empty());

	deleteUnusedBakedTextures(scan, TexturePruneDesc{ root.path });

	CHECK(std::filesystem::exists(root.textures() / "skybox.ktx2"));
	CHECK(std::filesystem::exists(root.textures() / "brdf_lut.ktx2"));
}

TEST_CASE("findUnusedBakedTextures refuses to run on an unreadable material", "[texture_prune]")
{
	// The fail-safe. A material we cannot parse is a material whose maps we cannot mark -- proceeding
	// would sweep them as garbage, so the scan aborts instead and nothing is deleted.
	const DataRoot root("bernini_prune_corrupt");

	writeSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	const BMaterial material = bakeAndSave(root, "good.bmaterial", "a.ktx2");

	std::ofstream(root.path / "Materials" / "broken.bmaterial", std::ios::binary)
		<< "not a material";

	REQUIRE_THROWS_AS(findUnusedBakedTextures(TexturePruneDesc{ root.path }), std::runtime_error);
	CHECK(std::filesystem::exists(root.path / material.baseColorTexture));
}

TEST_CASE("findUnusedBakedTextures handles a project with nothing baked", "[texture_prune]")
{
	const DataRoot root("bernini_prune_empty");

	SECTION("no texture directory is not an error: nothing baked, nothing orphaned")
	{
		const auto scan = findUnusedBakedTextures(TexturePruneDesc{ root.path });

		CHECK(scan.unused.empty());
		CHECK(scan.candidates == 0);
		CHECK(scan.materialsScanned == 0);
	}

	SECTION("a data root that does not exist is a caller error")
	{
		REQUIRE_THROWS_AS(
			findUnusedBakedTextures(TexturePruneDesc{ root.path / "nope" }),
			std::runtime_error);
	}
}

TEST_CASE("findUnusedBakedTextures honours a custom texture directory", "[texture_prune]")
{
	const DataRoot root("bernini_prune_texdir");

	writeSource(root.path / "a.ktx2", 16, { { 200, 0, 0, 255 } });
	writeSource(root.path / "b.ktx2", 16, { { 0, 200, 0, 255 } });

	auto desc       = TexturePruneDesc{ root.path };
	desc.textureDir = "cooked";

	auto bake       = MaterialBakeDesc{ root.path };
	bake.textureDir = "cooked";

	BMaterial material;
	material.routes[0] = { "a.ktx2", 0 };
	bakeMaterial(material, bake);
	const std::string orphan = material.baseColorTexture;

	material.routes[0] = { "b.ktx2", 0 };
	bakeMaterial(material, bake);
	saveMaterial(material, root.path / "Materials" / "mat.bmaterial");

	const auto scan = findUnusedBakedTextures(desc);

	REQUIRE(scan.unused.size() == 1);
	CHECK(scan.unused.front().path == orphan);
	CHECK(orphan.starts_with("cooked/"));

	CHECK(deleteUnusedBakedTextures(scan, desc).deleted == 1);
	CHECK_FALSE(std::filesystem::exists(root.path / orphan));
}
