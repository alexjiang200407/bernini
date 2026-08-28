#include <assetlib/envmap.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "MountAt.h"
#include "mounted_io.h"

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

	struct DataRoot
	{
		std::filesystem::path path;

		explicit DataRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}

		~DataRoot() { std::filesystem::remove_all(path); }
	};

	bool
	SamePixels(const ImageData& a, const ImageData& b)
	{
		return a.width == b.width && a.mipLevels == b.mipLevels && a.vkFormat == b.vkFormat &&
		       a.isCubemap == b.isCubemap && a.pixels.size() == b.pixels.size() &&
		       std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()) == 0;
	}

	/** Writes a complete authored set under `root` and returns the .benv's path. */
	std::filesystem::path
	AuthoredSet(const DataRoot& root)
	{
		writeKTX2(GradientCube(16, 2.0f), root.path / "sky.ktx2", false, Ktx2Compression::kNone);
		writeKTX2(GradientCube(8, 1.0f), root.path / "pre.ktx2", false, Ktx2Compression::kNone);
		writeKTX2(GradientCube(4, 0.25f), root.path / "irr.ktx2", false, Ktx2Compression::kNone);

		BSky sky;
		sky.name      = "set";
		sky.sky.baked = "sky.ktx2";
		StoreAt(root.path).Save(sky, "set.bsky");

		BEnvLighting lighting;
		lighting.name             = "set";
		lighting.prefilter.baked  = "pre.ktx2";
		lighting.irradiance.baked = "irr.ktx2";
		lighting.exposure         = 1.375f;
		StoreAt(root.path).Save(lighting, "set.benvl");

		const auto envPath = root.path / "set.benv";
		SaveAt(
			BEnv{ .name         = "set",
		          .sky          = "set.bsky",
		          .lighting     = "set.benvl",
		          .skyMipLevel  = 2,
		          .skyRotationY = 0.5f },
			envPath);
		return envPath;
	}
}

TEST_CASE("resolving an environment loads the baked maps its chain names", "[benv][resolve]")
{
	const DataRoot root("bernini_env_resolve_full");

	const ResolvedEnvironment resolved = resolveEnvironment(AuthoredSet(root), MountAt(root.path));

	CHECK(SamePixels(resolved.maps.skybox, GradientCube(16, 2.0f)));
	CHECK(SamePixels(resolved.maps.prefilter, GradientCube(8, 1.0f)));
	CHECK(SamePixels(resolved.maps.irradiance, GradientCube(4, 0.25f)));
	CHECK(resolved.maps.exposure == Catch::Approx(1.375f));

	SECTION("the sky's presentation travels with it")
	{
		// The document asks for mip 2; the baked cube holds one level, so serving clamps.
		CHECK(resolved.skyMipLevel == 0);
		CHECK(resolved.skyRotationY == Catch::Approx(0.5f));
	}
}

TEST_CASE("resolving follows only what the .benv references", "[benv][resolve]")
{
	const DataRoot root("bernini_env_resolve");

	SECTION("an empty .benv resolves to an empty environment")
	{
		StoreAt(root.path).Save(BEnv{ .name = "none" }, "none.benv");
		const ResolvedEnvironment resolved =
			resolveEnvironment(root.path / "none.benv", MountAt(root.path));
		CHECK(resolved.maps.skybox.pixels.size() == 0);
		CHECK(resolved.maps.prefilter.pixels.size() == 0);
		CHECK(resolved.maps.exposure == Catch::Approx(1.0f));
	}

	// The state a fresh checkout is in: the baked maps are regenerated per platform and kept out of
	// source control, so a sky arrives with its float source and nothing compiled from it.
	SECTION("a referenced sky that was never baked resolves to its source")
	{
		writeKTX2(
			GradientCube(16, 3.0f),
			root.path / "raw_src.ktx2",
			false,
			Ktx2Compression::kNone);

		BSky sky;
		sky.name       = "raw";
		sky.sky.source = "raw_src.ktx2";
		StoreAt(root.path).Save(sky, "raw.bsky");
		SaveAt(BEnv{ .name = "raw", .sky = "raw.bsky" }, root.path / "raw.benv");

		const ResolvedEnvironment resolved =
			resolveEnvironment(root.path / "raw.benv", MountAt(root.path));
		CHECK(SamePixels(resolved.maps.skybox, GradientCube(16, 3.0f)));
	}

	SECTION("a referenced sky with neither a baked map nor a source throws")
	{
		BSky sky;
		sky.name       = "raw";
		sky.sky.source = "Derived/SourceTextures/raw.ktx2";
		StoreAt(root.path).Save(sky, "raw.bsky");
		SaveAt(BEnv{ .name = "raw", .sky = "raw.bsky" }, root.path / "raw.benv");

		CHECK_THROWS_WITH(
			resolveEnvironment(root.path / "raw.benv", MountAt(root.path)),
			Catch::Matchers::ContainsSubstring("is on disk"));
	}

	SECTION("a dangling reference throws")
	{
		SaveAt(BEnv{ .name = "gone", .sky = "nowhere.bsky" }, root.path / "gone.benv");
		CHECK_THROWS_AS(
			resolveEnvironment(root.path / "gone.benv", MountAt(root.path)),
			std::runtime_error);
	}
}
