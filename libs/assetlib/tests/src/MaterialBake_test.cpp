#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>

#include "bmesh_texture.h"

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	// A scratch directory that cleans up after itself.
	struct BakeDir
	{
		std::filesystem::path path;

		explicit BakeDir(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~BakeDir() { std::filesystem::remove_all(path); }
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
}

TEST_CASE(
	"dilateColorIntoTransparent bleeds the nearest opaque color over garbage",
	"[bmaterial][bake]")
{
	// 8x1: opaque red at [0,1], opaque blue at [6,7], transparent *garbage gray* between -- the
	// arbitrary colour BC7 would otherwise store and fringe back across a cutout edge.
	std::array<std::byte, 8u * 4u> px{};
	const auto                     set = [&](size_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		px[i * 4u + 0u] = std::byte{ r };
		px[i * 4u + 1u] = std::byte{ g };
		px[i * 4u + 2u] = std::byte{ b };
		px[i * 4u + 3u] = std::byte{ a };
	};
	set(0u, 255, 0, 0, 255);
	set(1u, 255, 0, 0, 255);
	for (size_t i = 2u; i <= 5u; ++i) set(i, 128, 128, 128, 0);
	set(6u, 0, 0, 255, 255);
	set(7u, 0, 0, 255, 255);

	dilateColorIntoTransparent(px, 8, 1);

	const auto ch = [&](size_t i, size_t c) { return std::to_integer<uint8_t>(px[i * 4u + c]); };

	// Every texel is now red or blue -- the gray garbage is gone (green stays 0 throughout).
	for (size_t i = 0u; i < 8u; ++i)
	{
		CHECK(ch(i, 1u) == 0);
		CHECK((ch(i, 0u) == 255) != (ch(i, 2u) == 255));
	}

	// And it is the *nearest* opaque color: 2,3 take red, 4,5 take blue.
	CHECK(ch(2u, 0u) == 255);
	CHECK(ch(3u, 0u) == 255);
	CHECK(ch(4u, 2u) == 255);
	CHECK(ch(5u, 2u) == 255);

	// Alpha is untouched -- only the color bleeds.
	CHECK(ch(3u, 3u) == 0);
	CHECK(ch(0u, 3u) == 255);
}

TEST_CASE("bakeMaterial composites routes into the optimized triplet", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_material");

	// Two sources: an albedo and a packed map whose G is roughness and B is metallic.
	writeSource(dir.path / "albedo.ktx2", 16, { { 200, 100, 50, 255 } });
	writeSource(dir.path / "packed.ktx2", 16, { { 10, 60, 90, 255 } });

	BMaterial mat;
	mat.mode          = MaterialMode::kLoose;
	mat.pbr.routes[0] = { "albedo.ktx2", 0 };  // base R
	mat.pbr.routes[1] = { "albedo.ktx2", 1 };  // base G
	mat.pbr.routes[2] = { "albedo.ktx2", 2 };  // base B
	mat.pbr.routes[5] = { "packed.ktx2", 1 };  // roughness <- packed.G
	mat.pbr.routes[6] = { "packed.ktx2", 2 };  // metallic  <- packed.B

	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	SECTION("it fills the triplet and switches to the baked representation")
	{
		REQUIRE(mat.mode == MaterialMode::kBaked);
		REQUIRE(std::filesystem::exists(dir.path / mat.pbr.baseColorTexture));
		REQUIRE(std::filesystem::exists(dir.path / mat.pbr.ormTexture));
	}

	SECTION("baked maps land under the data root's texture directory, not beside the material")
	{
		// The recorded path is relative to the data root, whatever directory the material lives in.
		REQUIRE(mat.pbr.baseColorTexture.starts_with("Textures/basecolor_"));
		REQUIRE(mat.pbr.ormTexture.starts_with("Textures/orm_"));
		REQUIRE(mat.pbr.baseColorTexture.ends_with(".ktx2"));
	}

	SECTION("a group with nothing routed is not baked")
	{
		// No normal channel is routed, so no normal map is written; the runtime falls back to flat.
		REQUIRE(mat.pbr.normalTexture.empty());
		REQUIRE(
			std::ranges::none_of(
				std::filesystem::directory_iterator(dir.path / "Textures"),
				[](const auto& entry) {
					return entry.path().filename().string().starts_with("normal_");
				}));
	}

	SECTION("each map lands in its own block format")
	{
		REQUIRE(
			loadKTX2(dir.path / mat.pbr.baseColorTexture).vkFormat == VkFormat::BC1_RGB_SRGB_BLOCK);
		REQUIRE(loadKTX2(dir.path / mat.pbr.ormTexture).vkFormat == VkFormat::BC7_UNORM_BLOCK);
	}

	SECTION("the routes and the graph survive the bake")
	{
		// The whole point of the coexistence: a baked material can still be reopened and re-baked.
		REQUIRE(mat.pbr.routes[0].texture == "albedo.ktx2");
		REQUIRE(mat.pbr.routes[5].channel == 1);
	}

	SECTION("it stamps every routed source and leaves unrouted ones zeroed")
	{
		REQUIRE(mat.pbr.routeStamps[0] == stampOf(dir.path / "albedo.ktx2"));
		REQUIRE(mat.pbr.routeStamps[5] == stampOf(dir.path / "packed.ktx2"));
		REQUIRE(mat.pbr.routeStamps[3] == SourceStamp{});  // base A is unrouted
		REQUIRE_FALSE(bakeIsStale(mat, dir.path));
	}

	SECTION("editing a source makes the bake stale")
	{
		writeSource(dir.path / "albedo.ktx2", 32, { { 1, 2, 3, 255 } });  // different size
		REQUIRE(bakeIsStale(mat, dir.path));
	}
}

TEST_CASE("bakeMaterial keeps base-color alpha for a blend material", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_blend");

	// A base colour whose alpha channel actually carries data (A = 128, not opaque).
	writeSource(dir.path / "albedo.ktx2", 16, { { 200, 100, 50, 128 } });

	const auto bakeBaseColor = [&](AlphaMode mode) {
		BMaterial mat;
		mat.mode          = MaterialMode::kLoose;
		mat.pbr.routes[0] = { "albedo.ktx2", 0 };
		mat.pbr.routes[1] = { "albedo.ktx2", 1 };
		mat.pbr.routes[2] = { "albedo.ktx2", 2 };
		mat.pbr.routes[3] = { "albedo.ktx2", 3 };  // base A -- only meaningful once alpha is kept
		mat.pbr.alphaMode = mode;
		REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));
		return mat.pbr.baseColorTexture;
	};

	SECTION("a blend base color bakes to BC7, not the alpha-less BC1")
	{
		// BC1 would silently drop the alpha channel and there could be no blending.
		REQUIRE(
			loadKTX2(dir.path / bakeBaseColor(AlphaMode::kBlend)).vkFormat ==
			VkFormat::BC7_SRGB_BLOCK);
	}

	SECTION("cutout and blend do not converge on one baked map")
	{
		// Identical routes and both BC7, but cutout bakes coverage-preserving mips against its cutoff
		// and blend bakes plain ones -- different bytes, so they must not share a file.
		REQUIRE(bakeBaseColor(AlphaMode::kMask) != bakeBaseColor(AlphaMode::kBlend));
	}
}

namespace
{
	// The colour a BC1 texture's first block decodes to, read straight out of the block's two RGB565
	// endpoints. Our bake inputs are constant-coloured, so the endpoints bracket that colour: c0 is the
	// upper one and c1 the lower, and their midpoint is what every texel in the block decodes to. That
	// recovers (approximately) the texel that was composited -- enough to prove which channel is where.
	std::array<int, 3>
	firstBc1Color(const ImageData& image)
	{
		REQUIRE(image.vkFormat == VkFormat::BC1_RGB_SRGB_BLOCK);

		uint16_t c0 = 0;
		uint16_t c1 = 0;
		std::memcpy(&c0, image.pixels.data(), sizeof(c0));
		std::memcpy(&c1, image.pixels.data() + sizeof(c0), sizeof(c1));

		// Widen 5/6-bit channels to 8 bits the way a hardware decoder does.
		const auto expand = [](uint16_t c) {
			const int r5 = (c >> 11) & 0x1F;
			const int g6 = (c >> 5) & 0x3F;
			const int b5 = c & 0x1F;
			return std::array<int, 3>{
				{ (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2) }
			};
		};

		const auto hi = expand(c0);
		const auto lo = expand(c1);
		return { { (hi[0] + lo[0]) / 2, (hi[1] + lo[1]) / 2, (hi[2] + lo[2]) / 2 } };
	}
}

TEST_CASE("bakeMaterial routes each channel from its own source", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_routing");

	// Distinct constant sources, so each composited component can be traced back to exactly one.
	writeSource(dir.path / "a.ktx2", 16, { { 240, 0, 0, 255 } });
	writeSource(dir.path / "b.ktx2", 16, { { 0, 128, 0, 255 } });
	writeSource(dir.path / "c.ktx2", 16, { { 0, 0, 64, 255 } });

	BMaterial mat;
	mat.pbr.routes[0] = { "a.ktx2", 0 };  // base R <- a.R = 240
	mat.pbr.routes[1] = { "b.ktx2", 1 };  // base G <- b.G = 128
	mat.pbr.routes[2] = { "c.ktx2", 2 };  // base B <- c.B = 64

	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	const auto rgb = firstBc1Color(loadKTX2(dir.path / mat.pbr.baseColorTexture));

	// The UASTC -> BC1 round trip is lossy and BC1 quantizes to 5/6/5 bits, so allow a wide margin.
	// It is still far tighter than any channel swap: these three values are 64 apart or more.
	constexpr double c_Bc1Margin = 12.0;
	CHECK(rgb[0] == Catch::Approx(240).margin(c_Bc1Margin));
	CHECK(rgb[1] == Catch::Approx(128).margin(c_Bc1Margin));
	CHECK(rgb[2] == Catch::Approx(64).margin(c_Bc1Margin));
}

TEST_CASE("bakeMaterial fills an unrouted channel with its neutral value", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_neutral");

	writeSource(dir.path / "a.ktx2", 16, { { 240, 10, 10, 255 } });

	BMaterial mat;
	mat.pbr.routes[0] = { "a.ktx2", 0 };  // only base R is routed

	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	// G and B are unrouted, so they sample 1.0 and let baseColorFactor drive them.
	constexpr double c_Bc1Margin = 12.0;
	const auto       rgb         = firstBc1Color(loadKTX2(dir.path / mat.pbr.baseColorTexture));
	CHECK(rgb[0] == Catch::Approx(240).margin(c_Bc1Margin));
	CHECK(rgb[1] == Catch::Approx(255).margin(c_Bc1Margin));
	CHECK(rgb[2] == Catch::Approx(255).margin(c_Bc1Margin));
}

TEST_CASE("bakeMaterial resamples sources to the largest routed one", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_resample");

	writeSource(dir.path / "small.ktx2", 8, { { 255, 0, 0, 255 } });
	writeSource(dir.path / "big.ktx2", 32, { { 0, 255, 0, 255 } });

	BMaterial mat;
	mat.pbr.routes[0] = { "small.ktx2", 0 };
	mat.pbr.routes[1] = { "big.ktx2", 1 };

	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	// The composited map takes the largest source's dimensions, not the first route's.
	const ImageData baked = loadKTX2(dir.path / mat.pbr.baseColorTexture);
	REQUIRE(baked.width == 32);
	REQUIRE(baked.height == 32);
}

namespace
{
	size_t
	countMaps(const std::filesystem::path& textureDir, std::string_view prefix)
	{
		size_t count = 0;
		for (const auto& entry : std::filesystem::directory_iterator(textureDir))
			if (entry.path().filename().string().starts_with(prefix))
				++count;
		return count;
	}
}

TEST_CASE("materials that route a group identically share one baked map", "[bmaterial][bake]")
{
	// The Apple case: two submeshes, two materials, one shared ORM source. Their ORM groups route the
	// same channels of the same texture, so the composited output is byte-identical -- it must be one
	// file, not two copies.
	const BakeDir dir("bernini_bake_sharing");

	writeSource(dir.path / "orm.ktx2", 16, { { 10, 60, 90, 255 } });
	writeSource(dir.path / "albedo1.ktx2", 16, { { 200, 0, 0, 255 } });
	writeSource(dir.path / "albedo2.ktx2", 16, { { 0, 200, 0, 255 } });

	const auto ormRoutes = [](BMaterial& mat) {
		mat.pbr.routes[4] = { "orm.ktx2", 0 };
		mat.pbr.routes[5] = { "orm.ktx2", 1 };
		mat.pbr.routes[6] = { "orm.ktx2", 2 };
	};

	BMaterial apple1;
	apple1.pbr.routes[0] = { "albedo1.ktx2", 0 };
	ormRoutes(apple1);

	BMaterial apple2;
	apple2.pbr.routes[0] = { "albedo2.ktx2", 0 };  // a *different* base colour
	ormRoutes(apple2);

	REQUIRE_NOTHROW(bakeMaterial(apple1, MaterialBakeDesc{ dir.path }));
	REQUIRE_NOTHROW(bakeMaterial(apple2, MaterialBakeDesc{ dir.path }));

	const auto textures = dir.path / "Textures";

	SECTION("the shared group converges on one file")
	{
		REQUIRE(apple1.pbr.ormTexture == apple2.pbr.ormTexture);
		REQUIRE(countMaps(textures, "orm_") == 1);
	}

	SECTION("the groups that differ do not")
	{
		REQUIRE(apple1.pbr.baseColorTexture != apple2.pbr.baseColorTexture);
		REQUIRE(countMaps(textures, "basecolor_") == 2);
	}

	SECTION("a group's output does not depend on textures outside it")
	{
		// apple2's base colour source could have been a different size; its ORM map must not change.
		// Each group is sized to the largest source routed into that group, nothing else.
		writeSource(dir.path / "albedo2.ktx2", 64, { { 0, 200, 0, 255 } });

		BMaterial apple3;
		apple3.pbr.routes[0] = { "albedo2.ktx2", 0 };
		ormRoutes(apple3);

		REQUIRE_NOTHROW(bakeMaterial(apple3, MaterialBakeDesc{ dir.path }));
		REQUIRE(apple3.pbr.ormTexture == apple1.pbr.ormTexture);
		REQUIRE(countMaps(textures, "orm_") == 1);
	}
}

TEST_CASE("bakeMaterial re-encodes a map only when a source is newer", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_cache");

	writeSource(dir.path / "a.ktx2", 16, { { 200, 100, 50, 255 } });

	BMaterial mat;
	mat.pbr.routes[0] = { "a.ktx2", 0 };
	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	const auto baked = dir.path / mat.pbr.baseColorTexture;

	// Overwrite the baked map with a sentinel. Whether the next bake rewrites it is then observable
	// directly, rather than through an mtime whose resolution is only one second.
	const auto writeSentinel = [&]() {
		std::ofstream out(baked, std::ios::binary | std::ios::trunc);
		out << "SENTINEL";
	};
	const auto sentinelSurvives = [&]() {
		std::ifstream in(baked, std::ios::binary);
		std::string   text;
		in >> text;
		return text == "SENTINEL";
	};

	BMaterial again;
	again.pbr.routes[0] = { "a.ktx2", 0 };

	SECTION("nothing changed: the existing map is reused")
	{
		// Encoding a 4K map costs seconds, and a shared map is asked for once per material.
		writeSentinel();
		REQUIRE_NOTHROW(bakeMaterial(again, MaterialBakeDesc{ dir.path }));

		REQUIRE(sentinelSurvives());
		REQUIRE(again.pbr.baseColorTexture == mat.pbr.baseColorTexture);
	}

	SECTION("a source touched after the map was written forces a re-encode")
	{
		writeSentinel();
		std::filesystem::last_write_time(
			dir.path / "a.ktx2",
			std::filesystem::last_write_time(baked) + std::chrono::seconds(5));

		REQUIRE_NOTHROW(bakeMaterial(again, MaterialBakeDesc{ dir.path }));

		REQUIRE_FALSE(sentinelSurvives());
		REQUIRE(loadKTX2(baked).vkFormat == VkFormat::BC1_RGB_SRGB_BLOCK);
	}
}

TEST_CASE("bakeMaterial honours a custom texture directory", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_texdir");

	writeSource(dir.path / "a.ktx2", 16, { { 200, 100, 50, 255 } });

	BMaterial mat;
	mat.pbr.routes[0] = { "a.ktx2", 0 };

	auto desc       = MaterialBakeDesc{ dir.path };
	desc.textureDir = "cooked";
	REQUIRE_NOTHROW(bakeMaterial(mat, desc));

	REQUIRE(mat.pbr.baseColorTexture.starts_with("cooked/basecolor_"));
	REQUIRE(std::filesystem::exists(dir.path / mat.pbr.baseColorTexture));
}

TEST_CASE("bakeMaterial rejects a material with nothing routed", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_empty");

	BMaterial mat;
	REQUIRE_THROWS_AS(bakeMaterial(mat, MaterialBakeDesc{ dir.path }), std::runtime_error);
}

TEST_CASE("bakeMaterial accepts a Basis-supercompressed source", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_uastc");

	// This is what mesh import writes into textures_src, so it is the common case, not an edge one.
	// It is not RGBA8 on disk; the bake decodes it to texels rather than to the BC7 the GPU wants.
	std::vector<std::byte> pixels(16 * 16 * 4, std::byte{ 200 });
	writeKTX2(rgba8ToImage(pixels, 16, 16), dir.path / "uastc.ktx2", false);
	REQUIRE(loadKTX2(dir.path / "uastc.ktx2").vkFormat == VkFormat::BC7_UNORM_BLOCK);

	BMaterial mat;
	mat.pbr.routes[0] = { "uastc.ktx2", 0 };
	mat.pbr.routes[1] = { "uastc.ktx2", 1 };
	mat.pbr.routes[2] = { "uastc.ktx2", 2 };

	REQUIRE_NOTHROW(bakeMaterial(mat, MaterialBakeDesc{ dir.path }));

	const auto rgb = firstBc1Color(loadKTX2(dir.path / mat.pbr.baseColorTexture));
	CHECK(rgb[0] == Catch::Approx(200).margin(12));
}

TEST_CASE("bakeMaterial rejects an already-baked source", "[bmaterial][bake]")
{
	const BakeDir dir("bernini_bake_compressed");

	// A BC-compressed map carries no decodable texels, so it cannot be a bake input. Routing a
	// material's own baked output back into it is how an artist would hit this.
	std::vector<std::byte> pixels(16 * 16 * 4, std::byte{ 128 });
	writeKTX2(
		rgba8ToImage(pixels, 16, 16),
		dir.path / "baked.ktx2",
		false,
		Ktx2Compression::kBC7_RGBA);

	BMaterial mat;
	mat.pbr.routes[0] = { "baked.ktx2", 0 };

	REQUIRE_THROWS_AS(bakeMaterial(mat, MaterialBakeDesc{ dir.path }), std::runtime_error);
}

TEST_CASE("stripAuthoringData leaves only the shippable form", "[bmaterial][bake]")
{
	BMaterial mat;
	mat.mode                 = MaterialMode::kBaked;
	mat.pbr.baseColorTexture = "m_basecolor.ktx2";
	mat.pbr.routes[0]        = { "albedo.ktx2", 0 };
	mat.pbr.routeStamps[0]   = { 12, 34 };
	mat.editorGraph          = R"({"nodes":[]})";
	mat.pbr.roughnessFactor  = 0.25f;

	REQUIRE_NOTHROW(stripAuthoringData(mat));

	REQUIRE(mat.editorGraph.empty());
	REQUIRE(mat.pbr.routes[0].texture.empty());
	REQUIRE(mat.pbr.routeStamps[0] == SourceStamp{});
	REQUIRE(mat.mode == MaterialMode::kBaked);

	// What the runtime actually needs is untouched.
	REQUIRE(mat.pbr.baseColorTexture == "m_basecolor.ktx2");
	REQUIRE(mat.pbr.roughnessFactor == 0.25f);
}

TEST_CASE("stripAuthoringData refuses to strip an unbaked material", "[bmaterial][bake]")
{
	// Its routes are the only description of it; dropping them would render it undrawable.
	BMaterial mat;
	mat.mode          = MaterialMode::kLoose;
	mat.pbr.routes[0] = { "albedo.ktx2", 0 };

	REQUIRE_THROWS_AS(stripAuthoringData(mat), std::runtime_error);
}
