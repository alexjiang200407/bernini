#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include <catch2/catch_approx.hpp>

using namespace assetlib;

namespace
{
	BSky
	SampleSky()
	{
		BSky sky;
		sky.name       = "forest";
		sky.sky.source = "textures_src/forest_sky.ktx2";
		sky.sky.baked  = "Textures/sky_0123456789abcdef.ktx2";
		sky.sky.stamp  = SourceStamp{ 4096, 1700000000 };
		sky.mipLevel   = 3;
		sky.rotationY  = 1.25f;
		return sky;
	}

	BEnvLighting
	SampleLighting()
	{
		BEnvLighting lighting;
		lighting.name              = "forest";
		lighting.prefilter.source  = "textures_src/forest_prefilter.ktx2";
		lighting.prefilter.baked   = "Textures/prefilter_fedcba9876543210.ktx2";
		lighting.prefilter.stamp   = SourceStamp{ 8192, 1700000001 };
		lighting.irradiance.source = "textures_src/forest_irradiance.ktx2";
		lighting.irradiance.baked  = "Textures/irradiance_00ff00ff00ff00ff.ktx2";
		lighting.irradiance.stamp  = SourceStamp{ 1024, 1700000002 };
		lighting.exposure          = 0.375f;
		return lighting;
	}

	std::filesystem::path
	TempFile(const char* name)
	{
		return std::filesystem::temp_directory_path() / (std::string("bernini_") + name);
	}
}

TEST_CASE("a BSky survives a serialize round-trip", "[bsky][io]")
{
	const BSky sky      = SampleSky();
	const BSky restored = deserializeSky(serializeSky(sky));

	CHECK(restored.name == sky.name);
	CHECK(restored.sky == sky.sky);
	CHECK(restored.mipLevel == sky.mipLevel);
	CHECK(restored.rotationY == Catch::Approx(sky.rotationY));
}

TEST_CASE("a BEnvLighting survives a serialize round-trip", "[benvl][io]")
{
	const BEnvLighting lighting = SampleLighting();
	const BEnvLighting restored = deserializeEnvLighting(serializeEnvLighting(lighting));

	CHECK(restored.name == lighting.name);
	CHECK(restored.prefilter == lighting.prefilter);
	CHECK(restored.irradiance == lighting.irradiance);
	CHECK(restored.exposure == Catch::Approx(lighting.exposure));
}

// An unbaked asset is the state the editor writes first -- routed, never baked -- so the empty
// strings and zeroed stamps have to survive as themselves rather than collapsing into a short read.
TEST_CASE("an unrouted, unbaked environment asset round-trips as empty", "[bsky][benvl][io]")
{
	const BSky sky = deserializeSky(serializeSky(BSky{}));
	CHECK(sky.name.empty());
	CHECK(sky.sky.source.empty());
	CHECK(sky.sky.baked.empty());
	CHECK(sky.sky.stamp == SourceStamp{});
	CHECK(sky.mipLevel == 0);

	const BEnvLighting lighting = deserializeEnvLighting(serializeEnvLighting(BEnvLighting{}));
	CHECK(lighting.prefilter.source.empty());
	CHECK(lighting.irradiance.stamp == SourceStamp{});
	CHECK(lighting.exposure == Catch::Approx(1.0f));
}

TEST_CASE("the environment containers round-trip through a file", "[bsky][benvl][io]")
{
	const auto skyPath      = TempFile("env_container.bsky");
	const auto lightingPath = TempFile("env_container.benvl");

	saveSky(SampleSky(), skyPath);
	saveEnvLighting(SampleLighting(), lightingPath);

	CHECK(loadSky(skyPath).sky == SampleSky().sky);
	CHECK(loadEnvLighting(lightingPath).exposure == Catch::Approx(0.375f));

	std::filesystem::remove(skyPath);
	std::filesystem::remove(lightingPath);
}

// The two carry the same route shape, so a reader that only checked the length would accept the
// wrong file and report nonsense paths. The magic is what stops that, and these are the pairs that
// would actually be confused: they live in sibling directories and are written by the same import.
TEST_CASE("neither environment container reads the other's file", "[bsky][benvl][io]")
{
	CHECK_THROWS_AS(deserializeSky(serializeEnvLighting(SampleLighting())), std::runtime_error);
	CHECK_THROWS_AS(deserializeEnvLighting(serializeSky(SampleSky())), std::runtime_error);

	// ...nor a .benv's, which is the third file of the same import.
	CHECK(magic::c_BSky != magic::c_BEnv);
	CHECK(magic::c_BEnvL != magic::c_BEnv);
	CHECK(magic::c_BSky != magic::c_BEnvL);
}

TEST_CASE(
	"a truncated environment container throws rather than reading past the end",
	"[bsky][benvl][io]")
{
	auto bytes = serializeSky(SampleSky());
	bytes.resize(bytes.size() / 2);
	CHECK_THROWS_AS(deserializeSky(bytes), std::runtime_error);

	auto lightingBytes = serializeEnvLighting(SampleLighting());
	lightingBytes.resize(lightingBytes.size() / 2);
	CHECK_THROWS_AS(deserializeEnvLighting(lightingBytes), std::runtime_error);

	CHECK_THROWS_AS(deserializeSky({}), std::runtime_error);
}

// A major-version bump means the layout moved, so an old file must be refused rather than read as
// if the fields still lined up. Patched in place: the alternative is keeping a stale writer around.
TEST_CASE("an environment container from a future major version is refused", "[bsky][benvl][io]")
{
	auto bytes = serializeSky(SampleSky());
	REQUIRE(bytes.size() > 6);
	bytes[4] = std::byte{ 99 };
	bytes[5] = std::byte{ 0 };
	CHECK_THROWS_AS(deserializeSky(bytes), std::runtime_error);

	auto lightingBytes = serializeEnvLighting(SampleLighting());
	lightingBytes[4]   = std::byte{ 99 };
	lightingBytes[5]   = std::byte{ 0 };
	CHECK_THROWS_AS(deserializeEnvLighting(lightingBytes), std::runtime_error);
}
