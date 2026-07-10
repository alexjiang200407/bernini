#include <assetlib/image_io.h>

#include "bmesh_texture.h"

using namespace assetlib;

// An LDR texture bakes to Basis UASTC and transcodes back to BC7 on load, preserving dimensions,
// the mip chain, and (for base-color) the sRGB tag. Exercises the whole codec: mip generation,
// writeKTX2 compression, and loadKTX2 transcoding + block-aware subresource layout.
TEST_CASE("KTX2 LDR round-trips through Basis UASTC -> BC7", "[ktx2][io]")
{
	constexpr uint32_t w = 64;
	constexpr uint32_t h = 64;

	std::vector<std::byte> rgba(static_cast<size_t>(w) * h * 4, std::byte{ 180 });
	const ImageData        src = rgba8ToImage(rgba, w, h);

	REQUIRE(src.vkFormat == VkFormat::R8G8B8A8_UNORM);
	REQUIRE(src.width == w);
	REQUIRE(src.mipLevels == 7);  // floor(log2(64)) + 1

	const auto path = std::filesystem::temp_directory_path() / "bernini_ktx2_roundtrip.ktx2";

	SECTION("base-color (sRGB) -> BC7_SRGB")
	{
		writeKTX2(src, path, /*srgb*/ true);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.width == w);
		REQUIRE(loaded.height == h);
		REQUIRE(loaded.mipLevels == src.mipLevels);
		REQUIRE(loaded.vkFormat == VkFormat::BC7_SRGB_BLOCK);
		// BC7 is 4x4 16-byte blocks: base mip row pitch = ceil(64/4) * 16 = 256.
		REQUIRE(loaded.subresources.front().rowPitch == 256);

		std::filesystem::remove(path);
	}

	SECTION("linear (normal/ORM) -> BC7_UNORM")
	{
		writeKTX2(src, path, /*srgb*/ false);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.vkFormat == VkFormat::BC7_UNORM_BLOCK);
		REQUIRE(loaded.mipLevels == src.mipLevels);

		std::filesystem::remove(path);
	}
}

// The same Basis payload loadKTX2 turns into BC7 blocks transcodes to RGBA8 instead, so a CPU
// consumer (an editor thumbnail) needs no block decoder.
TEST_CASE("KTX2 preview decodes to uncompressed RGBA8", "[ktx2][io][preview]")
{
	constexpr uint32_t w = 256;
	constexpr uint32_t h = 256;

	std::vector<std::byte> rgba(static_cast<size_t>(w) * h * 4, std::byte{ 0 });
	for (size_t i = 0; i < rgba.size(); i += 4)
	{
		rgba[i + 0] = std::byte{ 200 };  // R
		rgba[i + 1] = std::byte{ 60 };   // G
		rgba[i + 2] = std::byte{ 30 };   // B
		rgba[i + 3] = std::byte{ 255 };  // A
	}

	const ImageData src  = rgba8ToImage(rgba, w, h);
	const auto      path = std::filesystem::temp_directory_path() / "bernini_ktx2_preview.ktx2";

	SECTION("picks the smallest mip covering maxDim and keeps channel order")
	{
		writeKTX2(src, path, /*srgb*/ true);
		const ImageData preview = loadKTX2Preview(path, 64);

		REQUIRE(preview.width == 64);
		REQUIRE(preview.height == 64);
		REQUIRE(preview.mipLevels == 1);
		REQUIRE(preview.arraySize == 1);
		REQUIRE_FALSE(preview.isCubemap);
		REQUIRE(preview.vkFormat == VkFormat::R8G8B8A8_SRGB);

		// Tightly packed, exactly one subresource.
		REQUIRE(preview.subresources.size() == 1);
		REQUIRE(preview.subresources.front().rowPitch == 64 * 4);
		REQUIRE(preview.pixels.size() == static_cast<size_t>(64) * 64 * 4);

		// A flat colour survives UASTC round-trip closely; assert R > B so a BGRA swizzle would fail.
		const auto* p = reinterpret_cast<const uint8_t*>(preview.pixels.data());
		CHECK(p[0] > 150);
		CHECK(p[2] < 80);
		CHECK(p[0] > p[2]);
		CHECK(p[3] == 255);

		std::filesystem::remove(path);
	}

	SECTION("linear source keeps its UNORM tag")
	{
		writeKTX2(src, path, /*srgb*/ false);
		REQUIRE(loadKTX2Preview(path, 32).vkFormat == VkFormat::R8G8B8A8_UNORM);
		std::filesystem::remove(path);
	}

	SECTION("maxDim larger than the image returns the base mip")
	{
		writeKTX2(src, path, /*srgb*/ true);
		const ImageData preview = loadKTX2Preview(path, 4096);

		REQUIRE(preview.width == w);
		REQUIRE(preview.height == h);

		std::filesystem::remove(path);
	}

	SECTION("uncompressed (no Basis payload) decodes without transcoding")
	{
		writeKTX2(src, path, /*srgb*/ true, /*compress*/ false);
		const ImageData preview = loadKTX2Preview(path, 64);

		REQUIRE(preview.width == 64);
		REQUIRE(preview.vkFormat == VkFormat::R8G8B8A8_SRGB);

		// Verbatim storage: the flat colour comes back exactly.
		const auto* p = reinterpret_cast<const uint8_t*>(preview.pixels.data());
		CHECK(p[0] == 200);
		CHECK(p[1] == 60);
		CHECK(p[2] == 30);

		std::filesystem::remove(path);
	}

	SECTION("maxDim of zero is rejected")
	{
		writeKTX2(src, path, /*srgb*/ true);
		REQUIRE_THROWS_AS(loadKTX2Preview(path, 0), std::runtime_error);
		std::filesystem::remove(path);
	}
}

// libktx initialises its Basis codec tables on first use behind a non-atomic `static bool`
// (lib/basis_transcode.cpp), so concurrent first transcodes race. The editor hits this: texture
// previews decode on a worker pool while the UI thread loads the same textures for the GPU.
// assetlib serialises until the init has completed, which this exercises.
TEST_CASE("KTX2 decodes concurrently from several threads", "[ktx2][io][threading]")
{
	constexpr uint32_t w        = 128;
	constexpr uint32_t h        = 128;
	constexpr int      kThreads = 4;

	const std::vector<std::byte> rgba(static_cast<size_t>(w) * h * 4, std::byte{ 120 });
	const auto path = std::filesystem::temp_directory_path() / "bernini_ktx2_threads.ktx2";
	writeKTX2(rgba8ToImage(rgba, w, h), path, /*srgb*/ true);

	std::vector<std::thread> threads;
	std::atomic<int>         decoded{ 0 };
	std::atomic<int>         failures{ 0 };

	for (int i = 0; i < kThreads; ++i)
	{
		// Alternate the two transcode targets so both the BC7 and RGBA8 paths run concurrently.
		threads.emplace_back([&, i]() {
			try
			{
				const ImageData image = (i % 2 == 0) ? loadKTX2(path) : loadKTX2Preview(path, 64);
				if (image.width > 0 && image.height > 0 && !image.pixels.empty())
					++decoded;
				else
					++failures;
			}
			catch (const std::exception&)
			{
				++failures;
			}
		});
	}

	for (std::thread& thread : threads) thread.join();

	CHECK(failures.load() == 0);
	CHECK(decoded.load() == kThreads);

	std::filesystem::remove(path);
}
