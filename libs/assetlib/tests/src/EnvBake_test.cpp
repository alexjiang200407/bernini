#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	/** A cube map of one uniform radiance, so the derived exposure has a known exact value. */
	ImageData
	ConstantCube(uint32_t size, float radiance)
	{
		ImageData out;
		out.width     = size;
		out.height    = size;
		out.mipLevels = 1;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		const size_t perFace = static_cast<size_t>(size) * size;
		out.pixels           = core::fixed_buffer<std::byte>(perFace * 6 * 4 * sizeof(float));

		auto*      px    = reinterpret_cast<float*>(out.pixels.data());
		const auto pitch = static_cast<uint64_t>(size) * 4 * sizeof(float);
		for (size_t t = 0; t < perFace * 6; ++t)
		{
			px[t * 4 + 0] = radiance;
			px[t * 4 + 1] = radiance;
			px[t * 4 + 2] = radiance;
			px[t * 4 + 3] = 1.0f;
		}
		for (uint32_t face = 0; face < 6; ++face)
			out.subresources.push_back({ face * perFace * 4 * sizeof(float), pitch, pitch * size });

		return out;
	}

	/** A fresh data root under the system temp dir, deleted when the test ends. */
	struct DataRoot
	{
		std::filesystem::path path;

		explicit DataRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path / "textures_src");
		}

		~DataRoot() { std::filesystem::remove_all(path); }

		/** Writes a float cube of `radiance` under textures_src/ and returns its data-root path. */
		std::string
		AddSource(const char* name, uint32_t size, float radiance) const
		{
			const auto relative = std::filesystem::path("textures_src") / name;
			writeKTX2(ConstantCube(size, radiance), path / relative, false, Ktx2Compression::kNone);
			return relative.generic_string();
		}
	};

	BSky
	RoutedSky(const DataRoot& root, float radiance = 1.0f)
	{
		BSky sky;
		sky.name       = "test";
		sky.sky.source = root.AddSource("sky_src.ktx2", 8, radiance);
		return sky;
	}

	BEnvLighting
	RoutedLighting(const DataRoot& root, float radiance = 1.0f)
	{
		BEnvLighting lighting;
		lighting.name              = "test";
		lighting.prefilter.source  = root.AddSource("prefilter_src.ktx2", 8, radiance);
		lighting.irradiance.source = root.AddSource("irradiance_src.ktx2", 4, radiance);
		return lighting;
	}
}

TEST_CASE("bakeSky compiles the routed source into a shipping RGB9E5 map", "[envbake]")
{
	const DataRoot root("bernini_envbake_sky");

	BSky sky = RoutedSky(root);
	bakeSky(sky, { root.path });

	REQUIRE(!sky.sky.baked.empty());
	CHECK(sky.sky.stamp == stampOf(root.path / sky.sky.source));

	const auto bakedName = std::filesystem::path(sky.sky.baked).filename().string();
	CHECK(isBakedEnvMapName(bakedName));
	CHECK(!isBakedMapName(bakedName));

	const ImageData baked = loadKTX2(root.path / sky.sky.baked);
	CHECK(baked.vkFormat == VkFormat::E5B9G9R9_UFLOAT_PACK32);
	CHECK(baked.isCubemap);
	CHECK(baked.width == 8);

	SECTION("the bake is stable: a second run reuses the same file")
	{
		BSky again = RoutedSky(root);
		bakeSky(again, { root.path });
		CHECK(again.sky.baked == sky.sky.baked);
	}
}

TEST_CASE("bakeEnvLighting bakes both maps and re-derives the exposure", "[envbake]")
{
	const DataRoot root("bernini_envbake_lighting");

	// exposureFor returns 0.18 / (0.96 * mean * 0.18) = 1 / (0.96 * mean): the light an 18% grey
	// card (reflectance 0.96 under this convention) reflects, mapped to middle grey. A constant 0.5
	// environment therefore lands at 1 / 0.48.
	BEnvLighting lighting = RoutedLighting(root, 0.5f);
	bakeEnvLighting(lighting, { root.path });

	CHECK(!lighting.prefilter.baked.empty());
	CHECK(!lighting.irradiance.baked.empty());
	CHECK(lighting.prefilter.baked != lighting.irradiance.baked);
	CHECK(lighting.exposure == Catch::Approx(1.0 / (0.96 * 0.5)).epsilon(0.01));

	CHECK(isBakedEnvMapName(std::filesystem::path(lighting.prefilter.baked).filename().string()));
	CHECK(isBakedEnvMapName(std::filesystem::path(lighting.irradiance.baked).filename().string()));
}

TEST_CASE("the environment staleness checks mirror the material's", "[envbake]")
{
	const DataRoot root("bernini_envbake_stale");

	SECTION("unrouted is never stale; routed-but-never-baked always is")
	{
		CHECK_FALSE(isSkyBakeStale(BSky{}, root.path));
		CHECK_FALSE(isEnvLightingBakeStale(BEnvLighting{}, root.path));

		CHECK(isSkyBakeStale(RoutedSky(root), root.path));
		CHECK(isEnvLightingBakeStale(RoutedLighting(root), root.path));
	}

	SECTION("a bake settles it; editing the source unsettles it")
	{
		BSky sky = RoutedSky(root);
		bakeSky(sky, { root.path });
		CHECK_FALSE(isSkyBakeStale(sky, root.path));

		// A different face size changes the file's byte size, so the stamp moves even inside the
		// same mtime second.
		root.AddSource("sky_src.ktx2", 16, 1.0f);
		CHECK(isSkyBakeStale(sky, root.path));
	}

	SECTION("a deleted source reads as stale rather than as unchanged")
	{
		BSky sky = RoutedSky(root);
		bakeSky(sky, { root.path });
		std::filesystem::remove(root.path / sky.sky.source);
		CHECK(isSkyBakeStale(sky, root.path));
	}

	// The other half of "mirror the material's": naming a map is not the same as having one. A route
	// left claiming a deleted map would report up to date and then bind the renderer's default.
	SECTION("a deleted baked map is stale, however fresh the source is")
	{
		BSky sky = RoutedSky(root);
		bakeSky(sky, { root.path });
		REQUIRE_FALSE(isSkyBakeStale(sky, root.path));

		std::filesystem::remove(root.path / sky.sky.baked);
		CHECK(isSkyBakeStale(sky, root.path));
	}

	// The pair is one verdict, so either map going missing has to carry it.
	SECTION("either half of the lighting losing its map makes the pair stale")
	{
		BEnvLighting lighting = RoutedLighting(root);
		bakeEnvLighting(lighting, { root.path });
		REQUIRE_FALSE(isEnvLightingBakeStale(lighting, root.path));

		std::filesystem::remove(root.path / lighting.irradiance.baked);
		CHECK(isEnvLightingBakeStale(lighting, root.path));
	}
}

TEST_CASE("a route draws its baked map, and its source when it cannot", "[envbake]")
{
	const DataRoot root("bernini_envbake_draw");

	BSky sky = RoutedSky(root);
	bakeSky(sky, { root.path });
	const std::string source = sky.sky.source;
	const std::string baked  = sky.sky.baked;

	SECTION("a current bake is what is drawn") { CHECK(envMapToDraw(sky.sky, root.path) == baked); }

	// The case a fresh checkout is in: Textures/ is regenerated per platform and kept out of source
	// control, so the sources arrive and the bakes do not.
	SECTION("a baked map that was never written falls back to the source")
	{
		std::filesystem::remove(root.path / baked);
		CHECK(envMapToDraw(sky.sky, root.path) == source);

		BSky unbaked = RoutedSky(root);
		CHECK(envMapToDraw(unbaked.sky, root.path) == unbaked.sky.source);
	}

	// The material's rule: loose is not a representation a route with no source can draw, so the
	// baked map is kept even though the stamp says it no longer reflects anything.
	SECTION("a deleted source keeps the baked map")
	{
		std::filesystem::remove(root.path / source);
		CHECK(envMapToDraw(sky.sky, root.path) == baked);
	}

	SECTION("a stale bake is displaced by the source it drifted from")
	{
		root.AddSource("sky_src.ktx2", 16, 1.0f);
		REQUIRE(isSkyBakeStale(sky, root.path));
		CHECK(envMapToDraw(sky.sky, root.path) == source);
	}

	SECTION("neither on disk throws, naming both")
	{
		std::filesystem::remove(root.path / source);
		std::filesystem::remove(root.path / baked);
		CHECK_THROWS_AS(envMapToDraw(sky.sky, root.path), std::runtime_error);

		// An unrouted route names nothing at all, which is the same verdict by a different path.
		CHECK_THROWS_AS(envMapToDraw(EnvMapRoute{}, root.path), std::runtime_error);
	}
}

TEST_CASE("a cancelled or failed environment bake leaves the asset untouched", "[envbake]")
{
	const DataRoot root("bernini_envbake_cancel");

	SECTION("cancellation")
	{
		std::stop_source stop;
		stop.request_stop();

		BSky sky = RoutedSky(root);
		CHECK_THROWS_AS(bakeSky(sky, { root.path }, stop.get_token()), Cancelled);
		CHECK(sky.sky.baked.empty());
		CHECK(sky.sky.stamp == SourceStamp{});
	}

	SECTION("nothing routed")
	{
		BSky unrouted;
		CHECK_THROWS_AS(bakeSky(unrouted, { root.path }), std::runtime_error);

		// Half-routed lighting is refused too: the maps are convolutions of one radiance, and
		// baking one against a stale other is the drift the pair exists to prevent.
		BEnvLighting half;
		half.prefilter.source = root.AddSource("half.ktx2", 4, 1.0f);
		CHECK_THROWS_AS(bakeEnvLighting(half, { root.path }), std::runtime_error);
		CHECK(half.prefilter.baked.empty());
	}

	SECTION("a source that is not a float cube")
	{
		auto flat      = ConstantCube(4, 1.0f);
		flat.isCubemap = false;
		writeKTX2(flat, root.path / "textures_src" / "flat.ktx2", false, Ktx2Compression::kNone);

		BSky sky;
		sky.sky.source = "textures_src/flat.ktx2";
		CHECK_THROWS_AS(bakeSky(sky, { root.path }), std::runtime_error);
		CHECK(sky.sky.baked.empty());
	}

	SECTION("a missing source")
	{
		BSky sky;
		sky.sky.source = "textures_src/nowhere.ktx2";
		CHECK_THROWS_AS(bakeSky(sky, { root.path }), std::runtime_error);
	}
}

TEST_CASE(
	"the texture prune keeps referenced environment maps and sweeps orphans",
	"[envbake][prune]")
{
	const DataRoot root("bernini_envbake_prune");

	BSky sky = RoutedSky(root);
	bakeSky(sky, { root.path });
	saveSky(sky, root.path / "forest.bsky");

	BEnvLighting lighting = RoutedLighting(root);
	bakeEnvLighting(lighting, { root.path });
	saveEnvLighting(lighting, root.path / "forest.benvl");

	// An env-named map nothing references: the leftover of a re-bake whose route changed.
	const auto orphan = root.path / "Textures" / "sky_00000000deadbeef.ktx2";
	writeKTX2(ConstantCube(4, 1.0f), orphan, false, Ktx2Compression::kNone);

	const auto scan = findUnusedBakedTextures({ root.path });

	CHECK(scan.environmentsScanned == 2);
	CHECK(scan.liveMaps == 3);
	REQUIRE(scan.unused.size() == 1);
	CHECK(scan.unused.front().path == "Textures/sky_00000000deadbeef.ktx2");

	deleteUnusedBakedTextures(scan, { root.path });
	CHECK(!std::filesystem::exists(orphan));
	CHECK(std::filesystem::exists(root.path / sky.sky.baked));
	CHECK(std::filesystem::exists(root.path / lighting.prefilter.baked));
	CHECK(std::filesystem::exists(root.path / lighting.irradiance.baked));
}

// The fatal-on-unreadable rule extends to the new assets: a .bsky the prune cannot read is a .bsky
// whose baked map it cannot mark, and that map would be swept as garbage.
TEST_CASE("the prune refuses to scan past an unreadable environment asset", "[envbake][prune]")
{
	const DataRoot root("bernini_envbake_prune_bad");

	std::ofstream(root.path / "broken.bsky") << "not a container";
	CHECK_THROWS_AS(findUnusedBakedTextures({ root.path }), std::runtime_error);

	std::filesystem::remove(root.path / "broken.bsky");
	std::ofstream(root.path / "broken.benvl") << "not a container";
	CHECK_THROWS_AS(findUnusedBakedTextures({ root.path }), std::runtime_error);
}
