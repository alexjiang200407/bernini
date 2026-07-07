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
