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

	// A `size` x `size` RGBA8 mask: opaque wherever `inside(x, y)`, fully transparent elsewhere. The
	// colour is a constant leafy green; only the alpha matters here.
	std::vector<std::byte>
	makeMask(uint32_t size, const std::function<bool(uint32_t, uint32_t)>& inside)
	{
		std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4, std::byte{ 0 });

		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				const size_t t = (static_cast<size_t>(y) * size + x) * 4;

				pixels[t + 0] = std::byte{ 200 };
				pixels[t + 1] = std::byte{ 60 };
				pixels[t + 2] = std::byte{ 40 };
				pixels[t + 3] = inside(x, y) ? std::byte{ 255 } : std::byte{ 0 };
			}
		}
		return pixels;
	}

	// 1-texel-wide vertical stripes every `period` texels: coverage 1/period. Thin structure of exactly
	// the kind that dissolves under naive mipping -- averaging pulls its alpha under any sane cutoff
	// immediately.
	std::vector<std::byte>
	stripeMask(uint32_t size, uint32_t period)
	{
		return makeMask(size, [period](uint32_t x, uint32_t) { return (x % period) == 0; });
	}

	std::vector<std::byte>
	discMask(uint32_t size, float radiusFraction)
	{
		const float centre = static_cast<float>(size) * 0.5f;
		const float radius = static_cast<float>(size) * radiusFraction;

		return makeMask(size, [centre, radius](uint32_t x, uint32_t y) {
			const float dx = static_cast<float>(x) + 0.5f - centre;
			const float dy = static_cast<float>(y) + 0.5f - centre;
			return dx * dx + dy * dy <= radius * radius;
		});
	}

	// The fraction of texels in one mip of `image` whose alpha passes `cutoff` -- i.e. exactly what
	// survives the shader's alpha test at that level.
	double
	coverageOfMip(const ImageData& image, uint32_t mip, float cutoff)
	{
		const ImageSubresource& sub = image.subresources[mip];

		const uint32_t w = (std::max)(1u, image.width >> mip);
		const uint32_t h = (std::max)(1u, image.height >> mip);

		size_t passed = 0;
		for (uint32_t y = 0; y < h; ++y)
		{
			for (uint32_t x = 0; x < w; ++x)
			{
				const size_t offset =
					sub.offset + static_cast<size_t>(y) * sub.rowPitch + static_cast<size_t>(x) * 4;
				const auto alpha = std::to_integer<uint8_t>(image.pixels[offset + 3]);
				if (static_cast<float>(alpha) / 255.0f >= cutoff)
					++passed;
			}
		}
		return static_cast<double>(passed) / (static_cast<double>(w) * h);
	}

	// Writes a `size` x `size` uncompressed RGBA8 .ktx2 from raw pixels.
	void
	writeSource(
		const std::filesystem::path&  path,
		uint32_t                      size,
		const std::vector<std::byte>& pixels)
	{
		writeKTX2(rgba8ToImage(pixels, size, size), path, false, Ktx2Compression::kNone);
	}
}

TEST_CASE("a thin cutout mask survives mipping", "[bmaterial][alphatest]")
{
	// Averaging an alpha mask down a mip chain shrinks the area that survives the cutoff, so a naively
	// mipped cutout thins out and dissolves with distance. Thin stripes are the extreme case, and the
	// naive control below is what stops this passing for the wrong reason.
	constexpr uint32_t c_Size   = 64;
	constexpr uint32_t c_Period = 4;  // 1-in-4 stripes -> 25% coverage
	constexpr float    c_Cutoff = 0.5f;

	const auto mask = stripeMask(c_Size, c_Period);

	const ImageData naive     = rgba8ToImage(mask, c_Size, c_Size);
	const ImageData corrected = rgba8ToImage(mask, c_Size, c_Size, c_Cutoff);

	const double target = coverageOfMip(naive, 0, c_Cutoff);
	REQUIRE(target == Catch::Approx(0.25).margin(0.02));

	// Mip 0 is the authored mask either way -- the correction only ever touches smaller levels.
	REQUIRE(coverageOfMip(corrected, 0, c_Cutoff) == Catch::Approx(target));

	SECTION("naively, the mask is gone by mip 1")
	{
		// Averaging 1-in-4 opaque stripes lands alpha at ~25% of 255, well under the cutoff, so
		// *nothing* passes any more: the cutout has silently evaporated. Every smaller level is worse.
		for (uint32_t mip = 1; mip < naive.mipLevels; ++mip)
		{
			INFO("naive mip " << mip);
			CHECK(coverageOfMip(naive, mip, c_Cutoff) < 0.01);
		}
	}

	SECTION("corrected, it never thins below what was authored")
	{
		// The guarantee: the rescale picks the smallest scale whose coverage still *reaches* mip 0's,
		// so a level can come out slightly fat but never thin. Fat is the safe direction -- a cutout
		// that bulks up marginally at distance is invisible; one that dissolves is not.
		//
		// It only comes out fat at all because a grating aliases: one downsample turns 1-in-4 stripes
		// into near-uniform alpha, and coverage can then only step 0 -> 0.5 -> 1, with no scale landing
		// on 0.25. A real cutout is a blob, not a grating -- see the disc below, which lands close.
		for (uint32_t mip = 1; mip + 1 < corrected.mipLevels; ++mip)
		{
			const double coverage = coverageOfMip(corrected, mip, c_Cutoff);
			INFO("corrected mip " << mip << " coverage " << coverage << " target " << target);

			CHECK(coverage >= target);
		}
	}
}

TEST_CASE("a disc cutout holds its coverage closely", "[bmaterial][alphatest]")
{
	// The realistic case: a solid blob, not a grating. Its coverage does not collapse to a step
	// function under downsampling, so the rescale can land near the authored area rather than merely
	// bounding it from below.
	constexpr uint32_t c_Size   = 128;
	constexpr float    c_Cutoff = 0.5f;

	// Below this, a level simply has too few texels to *represent* the target coverage: a centred disc
	// is 4-fold symmetric, so its texels cross the cutoff in equal-alpha groups and coverage can only
	// step in quarters. The correction then overshoots to the next step, which is the safe direction
	// and is what the stripe case above pins down. Tight matching is only a meaningful thing to ask of
	// levels with enough texels to express the answer.
	constexpr uint32_t c_MinMeaningfulSize = 16;

	const auto mask = discMask(c_Size, 0.3f);  // ~28% coverage

	const ImageData naive     = rgba8ToImage(mask, c_Size, c_Size);
	const ImageData corrected = rgba8ToImage(mask, c_Size, c_Size, c_Cutoff);

	const double target = coverageOfMip(naive, 0, c_Cutoff);
	REQUIRE(target > 0.2);

	uint32_t checked = 0;
	for (uint32_t mip = 1; mip < corrected.mipLevels; ++mip)
	{
		if ((c_Size >> mip) < c_MinMeaningfulSize)
			break;

		const double got   = coverageOfMip(corrected, mip, c_Cutoff);
		const double eaten = coverageOfMip(naive, mip, c_Cutoff);
		INFO("mip " << mip << " corrected " << got << " naive " << eaten << " target " << target);

		// A level is still a step function of a shrinking texel count, so an exact match is impossible;
		// what matters is that the disc keeps its area instead of eroding away.
		CHECK(got == Catch::Approx(target).margin(0.08));

		// ...and that it is doing better than the averaging it replaces.
		CHECK(got >= eaten);
		++checked;
	}
	REQUIRE(checked >= 3);
}

TEST_CASE("a cutout's base color bakes to a format that keeps its alpha", "[bmaterial][alphatest]")
{
	const BakeDir dir("bernini_bake_cutout");

	writeSource(dir.path / "leaf.ktx2", 32, stripeMask(32, 4));

	// The alpha mode is authored -- in the editor, by ending the graph in an Alpha Tested Material
	// Output node rather than the opaque one. The bake reads it and never infers it.
	BMaterial cutout;
	cutout.pbr.alphaMode = AlphaMode::kMask;
	cutout.pbr.routes[0] = { "leaf.ktx2", 0 };  // base R
	cutout.pbr.routes[1] = { "leaf.ktx2", 1 };  // base G
	cutout.pbr.routes[2] = { "leaf.ktx2", 2 };  // base B
	cutout.pbr.routes[3] = { "leaf.ktx2", 3 };  // base A

	REQUIRE_NOTHROW(bakeMaterial(cutout, MaterialBakeDesc{ dir.path }));

	SECTION("it bakes BC7, not BC1")
	{
		// BC1 has no alpha at all (libktx's only BC1 target is documented "opaque only, no
		// punchthrough alpha support yet"), so baking a cutout to it would composite the mask and then
		// silently throw it away -- the shader would sample alpha = 1 everywhere and cut nothing out.
		const ImageData baked = loadKTX2(dir.path / cutout.pbr.baseColorTexture);
		REQUIRE(baked.vkFormat == VkFormat::BC7_SRGB_BLOCK);
	}

	SECTION("the bake does not overwrite the authored mode")
	{
		// Stored on the material rather than re-derived at load, because stripAuthoringData drops the
		// routes for a shipping build -- there would be nothing left to derive it from.
		REQUIRE(cutout.pbr.alphaMode == AlphaMode::kMask);
	}

	SECTION("an opaque material that routes alpha anyway is still opaque, still BC1")
	{
		// The regression this guards. Inferring "routes alpha => cutout" looks reasonable until you
		// meet an importer that wires all four channels of every texture out of habit: every material
		// in the project silently becomes a two-sided BC7 cutout that cuts nothing out, at double the
		// memory. Routing alpha is not a request to test against it -- ending the graph in the cutout
		// node is.
		BMaterial opaque;
		opaque.pbr.routes[0] = { "leaf.ktx2", 0 };
		opaque.pbr.routes[1] = { "leaf.ktx2", 1 };
		opaque.pbr.routes[2] = { "leaf.ktx2", 2 };
		opaque.pbr.routes[3] = { "leaf.ktx2", 3 };  // routed, and deliberately ignored

		REQUIRE_NOTHROW(bakeMaterial(opaque, MaterialBakeDesc{ dir.path }));

		const ImageData baked = loadKTX2(dir.path / opaque.pbr.baseColorTexture);
		CHECK(baked.vkFormat == VkFormat::BC1_RGB_SRGB_BLOCK);
		CHECK(opaque.pbr.alphaMode == AlphaMode::kOpaque);
	}

	SECTION("the cutout and opaque variants cannot collide on one file name")
	{
		// Identical routes, different alpha mode, therefore different format. The bake key hashes the
		// *resolved* compression, so the two name different files -- otherwise whichever baked second
		// would be read as the other, and the cutout would load a BC1 map with no alpha.
		BMaterial opaque;
		opaque.pbr.routes = cutout.pbr.routes;

		REQUIRE_NOTHROW(bakeMaterial(opaque, MaterialBakeDesc{ dir.path }));

		REQUIRE(opaque.pbr.baseColorTexture != cutout.pbr.baseColorTexture);
		CHECK(std::filesystem::exists(dir.path / opaque.pbr.baseColorTexture));
		CHECK(std::filesystem::exists(dir.path / cutout.pbr.baseColorTexture));
	}
}

TEST_CASE("alphaMode and alphaCutoff survive a .bmaterial round trip", "[bmaterial][alphatest]")
{
	const BakeDir dir("bernini_bake_alpha_io");

	BMaterial material;
	material.pbr.alphaMode        = AlphaMode::kBlend;
	material.pbr.alphaCutoff      = 0.25f;
	material.pbr.occlude          = true;
	material.pbr.baseColorTexture = "Textures/basecolor_dead.ktx2";

	const auto path = dir.path / "cutout.bmaterial";
	REQUIRE_NOTHROW(saveMaterial(material, path));

	const BMaterial loaded = loadMaterial(path);
	CHECK(loaded.pbr.alphaMode == AlphaMode::kBlend);
	CHECK(loaded.pbr.alphaCutoff == 0.25f);
	CHECK(loaded.pbr.occlude);
}

TEST_CASE("a stale .bmaterial is rejected, not silently misread", "[bmaterial][alphatest]")
{
	// Only the current major version loads. The point is that a stale file fails *loudly*: the
	// alternative to a version check is not "it still works", it is reading v4's bytes with v5's
	// layout and getting a material made of garbage.
	BMaterial material;
	material.pbr.alphaMode   = AlphaMode::kMask;
	material.pbr.alphaCutoff = 0.25f;

	std::vector<std::byte> bytes = serializeMaterial(material);

	// Rewrite the version word (major is the uint16 right after the 4-byte magic) to v4 and lop off the
	// two fields v5 appended, producing exactly what a v4 writer would have emitted.
	constexpr uint16_t c_V4 = 4;
	std::memcpy(bytes.data() + sizeof(uint32_t), &c_V4, sizeof(c_V4));
	bytes.resize(bytes.size() - (sizeof(uint32_t) + sizeof(float)));

	REQUIRE_THROWS_AS(deserializeMaterial(bytes), std::runtime_error);
}
