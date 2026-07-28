#include <assetlib/benv_io.h>
#include <assetlib/envmap_bake.h>
#include <assetlib_structs/ImageData.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	ImageData
	ConstantCube(uint32_t size, uint32_t mips, float radiance)
	{
		ImageData out;
		out.width     = size;
		out.height    = size;
		out.mipLevels = mips;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = VkFormat::R32G32B32A32_SFLOAT;

		size_t total = 0;
		for (uint32_t mip = 0; mip < mips; ++mip)
		{
			const uint32_t n = std::max(1u, size >> mip);
			total += static_cast<size_t>(n) * n * 6;
		}
		out.pixels = core::fixed_buffer<std::byte>(total * 4 * sizeof(float));

		auto*  px     = reinterpret_cast<float*>(out.pixels.data());
		size_t offset = 0;
		for (uint32_t face = 0; face < 6; ++face)
		{
			for (uint32_t mip = 0; mip < mips; ++mip)
			{
				const uint32_t n     = std::max(1u, size >> mip);
				const auto     pitch = static_cast<uint64_t>(n) * 4 * sizeof(float);
				out.subresources.push_back({ offset * 4 * sizeof(float), pitch, pitch * n });
				for (size_t t = 0; t < static_cast<size_t>(n) * n; ++t)
				{
					float* c = px + (offset + t) * 4;
					c[0] = c[1] = c[2] = radiance;
					c[3]               = 1.0f;
				}
				offset += static_cast<size_t>(n) * n;
			}
		}
		return out;
	}

	std::filesystem::path
	TempBenv(const char* stem)
	{
		return std::filesystem::temp_directory_path() / (std::string("bernini_") + stem + ".benv");
	}
}

// The whole point of the container is that the three maps cannot be separated, so the round trip has
// to preserve all three plus the exposure they were measured at -- geometry and pixels both.
TEST_CASE("a .benv round-trips its three maps and exposure", "[benv][io]")
{
	auto set       = EnvironmentMaps();
	set.prefilter  = ConstantCube(16, 5, 0.25f);
	set.irradiance = ConstantCube(8, 1, 0.5f);
	set.skybox     = ConstantCube(16, 1, 0.75f);
	set.exposure   = 1.339f;

	auto provenance       = EnvironmentProvenance();
	provenance.samples    = 2048;
	provenance.mipLevels  = 5;
	provenance.sourceHash = 0xdeadbeefcafef00dull;

	const auto path = TempBenv("roundtrip");
	writeBenv(set, path, provenance);

	auto                  got      = EnvironmentProvenance();
	const EnvironmentMaps reloaded = loadBenv(path, &got);

	CHECK(reloaded.exposure == Catch::Approx(1.339f));
	CHECK(got.samples == 2048);
	CHECK(got.mipLevels == 5);
	CHECK(got.sourceHash == 0xdeadbeefcafef00dull);

	REQUIRE(reloaded.prefilter.width == 16);
	REQUIRE(reloaded.prefilter.mipLevels == 5);
	REQUIRE(reloaded.prefilter.isCubemap);
	REQUIRE(reloaded.irradiance.width == 8);
	REQUIRE(reloaded.irradiance.mipLevels == 1);
	REQUIRE(reloaded.skybox.width == 16);

	// Each map keeps its own value, so a chunk-ordering slip cannot pass by looking plausible.
	const auto first = [](const ImageData& m) {
		return reinterpret_cast<const float*>(m.pixels.data() + m.subresources.front().offset)[0];
	};
	CHECK(first(reloaded.prefilter) == Catch::Approx(0.25f));
	CHECK(first(reloaded.irradiance) == Catch::Approx(0.5f));
	CHECK(first(reloaded.skybox) == Catch::Approx(0.75f));

	std::filesystem::remove(path);
}

// Chunks are complete .ktx2 blobs on purpose, so ktxinfo and ktx compare still work on one carved
// out of the file. That is worth a test because it is the property a bespoke pixel layout would
// silently lose -- and the ktx tools are what diagnose a bad environment map.
TEST_CASE("a .benv embeds each map as a readable .ktx2", "[benv][io]")
{
	auto set       = EnvironmentMaps();
	set.prefilter  = ConstantCube(8, 4, 1.0f);
	set.irradiance = ConstantCube(8, 1, 1.0f);
	set.skybox     = ConstantCube(8, 1, 1.0f);

	const auto path = TempBenv("chunks");
	writeBenv(set, path, {});

	std::ifstream in(path, std::ios::binary);
	auto          bytes = std::vector<char>(std::istreambuf_iterator<char>(in), {});
	in.close();

	// The KTX2 identifier, once per embedded map.
	const char ktxId[] = { '\xAB', 'K', 'T', 'X', ' ', '2', '0', '\xBB' };
	int        found   = 0;
	for (size_t i = 0; i + sizeof(ktxId) <= bytes.size(); ++i)
		if (std::memcmp(bytes.data() + i, ktxId, sizeof(ktxId)) == 0)
			++found;

	CHECK(found == 3);
	std::filesystem::remove(path);
}

// Every misread of this container yields a plausible-looking environment, so it refuses rather than
// guesses. A version bump in particular must not be interpreted by an older build.
TEST_CASE("a .benv is rejected rather than misread", "[benv][io]")
{
	auto set       = EnvironmentMaps();
	set.prefilter  = ConstantCube(8, 4, 1.0f);
	set.irradiance = ConstantCube(8, 1, 1.0f);
	set.skybox     = ConstantCube(8, 1, 1.0f);

	const auto path = TempBenv("corrupt");
	writeBenv(set, path, {});

	auto bytes = std::vector<char>();
	{
		std::ifstream in(path, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(in), {});
	}

	const auto rewrite = [&](const std::vector<char>& data) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
	};

	SECTION("a bad magic")
	{
		auto bad = bytes;
		bad[0]   = 'X';
		rewrite(bad);
		CHECK_THROWS_AS(loadBenv(path), std::runtime_error);
	}

	SECTION("an unknown major version")
	{
		auto bad = bytes;
		bad[4]   = 99;  // versionMajor
		rewrite(bad);
		CHECK_THROWS_AS(loadBenv(path), std::runtime_error);
	}

	SECTION("a truncated file")
	{
		auto bad = bytes;
		bad.resize(bad.size() / 2);
		rewrite(bad);
		CHECK_THROWS_AS(loadBenv(path), std::runtime_error);
	}

	SECTION("a file that is not there at all")
	{
		CHECK_THROWS_AS(loadBenv(TempBenv("absent_qwerty")), std::runtime_error);
	}

	std::filesystem::remove(path);
}

TEST_CASE("writeBenv refuses an incomplete set", "[benv][io]")
{
	auto set      = EnvironmentMaps();
	set.prefilter = ConstantCube(8, 4, 1.0f);
	// irradiance and skybox left empty

	CHECK_THROWS_AS(writeBenv(set, TempBenv("incomplete"), {}), std::runtime_error);
}

// Exposure is stored because it is a property of the maps, not of the scene, and must be re-derived
// whenever they change. docs/envmaps.md works this out by hand for forest.hdr and gets ~1.33.
TEST_CASE("exposure is derived from the irradiance map", "[envmap][irradiance]")
{
	// A constant environment of this radiance is what forest.hdr measures, so the documented
	// arithmetic applies directly.
	const float exposure = exposureFor(ConstantCube(16, 1, 0.781f));
	CHECK(exposure == Catch::Approx(1.33f).epsilon(0.02));

	// Twice as bright an environment needs half the exposure.
	CHECK(exposureFor(ConstantCube(16, 1, 1.562f)) == Catch::Approx(exposure * 0.5f).epsilon(0.02));
}
