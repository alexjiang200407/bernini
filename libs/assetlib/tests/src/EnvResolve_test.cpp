#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_resolve.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <core/file/file.h>

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;

namespace
{
	/** A single-mip cube whose texels count up, so a byte mismatch cannot cancel out. */
	ImageData
	GradientCube(uint32_t size, float base)
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
			px[t * 4 + 0] = base + static_cast<float>(t) * 0.001f;
			px[t * 4 + 1] = base;
			px[t * 4 + 2] = base * 0.5f;
			px[t * 4 + 3] = 1.0f;
		}
		for (uint32_t face = 0; face < 6; ++face)
			out.subresources.push_back({ face * perFace * 4 * sizeof(float), pitch, pitch * size });

		return out;
	}

	struct SplitDir
	{
		std::filesystem::path path;

		explicit SplitDir(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}

		~SplitDir() { std::filesystem::remove_all(path); }
	};

	bool
	SamePixels(const ImageData& a, const ImageData& b)
	{
		return a.width == b.width && a.mipLevels == b.mipLevels && a.vkFormat == b.vkFormat &&
		       a.isCubemap == b.isCubemap && a.pixels.size() == b.pixels.size() &&
		       std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()) == 0;
	}
}

// The reason the split exists: the source .hdr of an existing .benv may be long gone, so the
// migration must reproduce, to the byte, what the v1 loader produced -- or every golden image
// derived from that environment moves.
TEST_CASE(
	"a v1 .benv splits into a reference set that resolves to the same pixels",
	"[benv][split]")
{
	const SplitDir dir("bernini_benv_split");

	EnvironmentMaps maps;
	maps.prefilter  = GradientCube(8, 1.0f);
	maps.irradiance = GradientCube(4, 0.25f);
	maps.skybox     = GradientCube(16, 2.0f);
	maps.exposure   = 1.375f;

	const auto v1Path = dir.path / "old.benv";
	writeBenv(maps, v1Path);

	const auto envPath = splitBenv(v1Path, dir.path / "out", "forest");

	const EnvironmentMaps     v1 = loadBenv(v1Path);
	const ResolvedEnvironment v2 = resolveEnvironment(envPath, dir.path / "out");

	CHECK(SamePixels(v2.maps.skybox, v1.skybox));
	CHECK(SamePixels(v2.maps.prefilter, v1.prefilter));
	CHECK(SamePixels(v2.maps.irradiance, v1.irradiance));
	CHECK(v2.maps.exposure == Catch::Approx(v1.exposure));
	CHECK(v2.skyMipLevel == 0);
	CHECK(v2.skyRotationY == 0.0f);

	SECTION("the split assets carry no sources, so they are never stale")
	{
		const BSky sky = loadSky(dir.path / "out" / "forest.bsky");
		CHECK(sky.sky.source.empty());
		CHECK(!sky.sky.baked.empty());

		const BEnvLighting lighting = loadEnvLighting(dir.path / "out" / "forest.benvl");
		CHECK(lighting.prefilter.source.empty());
		CHECK(lighting.exposure == Catch::Approx(1.375f));
	}

	SECTION("the split maps are the v1 blobs, byte for byte")
	{
		// Stronger than pixel equality: the KTX2 container itself must be untouched, or external
		// tools would see a different file than the v1 embedded.
		const auto blob =
			core::file::read_file_bytes((dir.path / "out" / "forest_sky.ktx2").string());
		CHECK(blob.size() > 0);
		const auto whole = core::file::read_file_bytes(v1Path.string());
		CHECK(std::search(whole.begin(), whole.end(), blob.begin(), blob.end()) != whole.end());
	}
}

TEST_CASE("resolving follows only what the .benv references", "[benv][resolve]")
{
	const SplitDir dir("bernini_env_resolve");

	SECTION("an empty .benv resolves to an empty environment")
	{
		saveEnv(BEnv{ .name = "none" }, dir.path / "none.benv");
		const ResolvedEnvironment resolved = resolveEnvironment(dir.path / "none.benv", dir.path);
		CHECK(resolved.maps.skybox.pixels.size() == 0);
		CHECK(resolved.maps.prefilter.pixels.size() == 0);
		CHECK(resolved.maps.exposure == Catch::Approx(1.0f));
	}

	SECTION("a referenced sky that was never baked throws rather than loading its source")
	{
		BSky sky;
		sky.name       = "raw";
		sky.sky.source = "textures_src/raw.ktx2";
		saveSky(sky, dir.path / "raw.bsky");
		saveEnv(BEnv{ .name = "raw", .sky = "raw.bsky" }, dir.path / "raw.benv");

		CHECK_THROWS_WITH(
			resolveEnvironment(dir.path / "raw.benv", dir.path),
			Catch::Matchers::ContainsSubstring("never been baked"));
	}

	SECTION("a dangling reference throws")
	{
		saveEnv(BEnv{ .name = "gone", .sky = "nowhere.bsky" }, dir.path / "gone.benv");
		CHECK_THROWS_AS(resolveEnvironment(dir.path / "gone.benv", dir.path), std::runtime_error);
	}

	SECTION("the sky's presentation travels with it")
	{
		const SplitDir  src("bernini_env_resolve_src");
		EnvironmentMaps maps;
		maps.prefilter  = GradientCube(4, 1.0f);
		maps.irradiance = GradientCube(4, 0.25f);
		maps.skybox     = GradientCube(4, 2.0f);
		writeBenv(maps, src.path / "p.benv");
		const auto envPath = splitBenv(src.path / "p.benv", dir.path, "p");

		BSky sky      = loadSky(dir.path / "p.bsky");
		sky.mipLevel  = 2;
		sky.rotationY = 0.5f;
		saveSky(sky, dir.path / "p.bsky");

		const ResolvedEnvironment resolved = resolveEnvironment(envPath, dir.path);
		CHECK(resolved.skyMipLevel == 2);
		CHECK(resolved.skyRotationY == Catch::Approx(0.5f));
	}
}
