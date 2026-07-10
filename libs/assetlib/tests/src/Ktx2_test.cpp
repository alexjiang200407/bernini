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

	SECTION("uncompressed is stored verbatim")
	{
		writeKTX2(src, path, /*srgb*/ false, Ktx2Compression::kNone);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.vkFormat == VkFormat::R8G8B8A8_UNORM);
		REQUIRE(loaded.mipLevels == src.mipLevels);

		std::filesystem::remove(path);
	}
}

// The bake targets: each writes a KTX2 that already carries its block format, so loadKTX2 hands it
// straight to the GPU rather than transcoding. This is what makes a baked material cheaper to load
// than the UASTC textures a mesh import emits.
TEST_CASE("KTX2 bake targets write their block format directly", "[ktx2][io][bake]")
{
	constexpr uint32_t w = 64;
	constexpr uint32_t h = 64;

	std::vector<std::byte> rgba(static_cast<size_t>(w) * h * 4, std::byte{ 180 });
	const ImageData        src = rgba8ToImage(rgba, w, h);

	const auto path = std::filesystem::temp_directory_path() / "bernini_ktx2_bake.ktx2";

	SECTION("base color -> BC1_RGB_SRGB")
	{
		writeKTX2(src, path, /*srgb*/ true, Ktx2Compression::kBC1_RGB);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.vkFormat == VkFormat::BC1_RGB_SRGB_BLOCK);
		REQUIRE(loaded.width == w);
		REQUIRE(loaded.mipLevels == src.mipLevels);
		// BC1 is 4x4 8-byte blocks: base mip row pitch = ceil(64/4) * 8 = 128.
		REQUIRE(loaded.subresources.front().rowPitch == 128);

		std::filesystem::remove(path);
	}

	SECTION("orm -> BC7_UNORM")
	{
		writeKTX2(src, path, /*srgb*/ false, Ktx2Compression::kBC7_RGBA);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.vkFormat == VkFormat::BC7_UNORM_BLOCK);
		REQUIRE(loaded.subresources.front().rowPitch == 256);

		std::filesystem::remove(path);
	}

	SECTION("normal -> BC5_UNORM")
	{
		writeKTX2(src, path, /*srgb*/ false, Ktx2Compression::kBC5_RG);
		const ImageData loaded = loadKTX2(path);

		REQUIRE(loaded.vkFormat == VkFormat::BC5_UNORM_BLOCK);
		REQUIRE(loaded.subresources.front().rowPitch == 256);

		std::filesystem::remove(path);
	}
}
