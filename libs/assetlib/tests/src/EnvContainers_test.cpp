#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include <core/io/ByteWriter.h>

#include "mounted_io.h"
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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

TEST_CASE("a BEnv survives a serialize round-trip", "[benv][io]")
{
	BEnv env;
	env.name     = "forest";
	env.sky      = "Sky/forest.bsky";
	env.lighting = "EnvLighting/forest.benvl";

	const BEnv restored = deserializeEnv(serializeEnv(env));
	CHECK(restored.name == env.name);
	CHECK(restored.sky == env.sky);
	CHECK(restored.lighting == env.lighting);

	// Half-composed is a legal file: the import's checkboxes write whichever pieces were asked for,
	// and what a .benv must reference is its consumer's rule, not the container's.
	const BEnv empty = deserializeEnv(serializeEnv(BEnv{}));
	CHECK(empty.name.empty());
	CHECK(empty.sky.empty());
	CHECK(empty.lighting.empty());
}

TEST_CASE("a BEnv round-trips through a file", "[benv][io]")
{
	const auto path = TempFile("env_container.benv");

	BEnv env;
	env.name     = "forest";
	env.sky      = "Sky/forest.bsky";
	env.lighting = "EnvLighting/forest.benvl";
	saveEnv(env, path);

	const BEnv restored = loadEnv(path);
	CHECK(restored.sky == env.sky);
	CHECK(restored.lighting == env.lighting);

	std::filesystem::remove(path);
}

// A v1 .benv opens with the same magic and the same version-field layout, so the reference reader
// sees exactly "version 1" -- and must refuse it by number, because what follows is KTX2 blobs that
// would otherwise be read as string lengths.
TEST_CASE("the reference reader refuses a v1 .benv and the other containers' files", "[benv][io]")
{
	core::io::ByteWriter v1;
	v1.WritePod(magic::c_BEnv);
	v1.WritePod<uint16_t>(1);
	v1.WritePod<uint16_t>(0);
	v1.WritePod<uint64_t>(0);  // the v1 header continues; the reader must not get that far
	CHECK_THROWS_WITH(deserializeEnv(v1.Take()), Catch::Matchers::ContainsSubstring("re-imported"));

	CHECK_THROWS_AS(deserializeEnv(serializeSky(SampleSky())), std::runtime_error);
	CHECK_THROWS_AS(deserializeEnv(serializeEnvLighting(SampleLighting())), std::runtime_error);

	auto truncated = serializeEnv(BEnv{ .name = "forest", .sky = "Sky/forest.bsky" });
	truncated.resize(truncated.size() / 2);
	CHECK_THROWS_AS(deserializeEnv(truncated), std::runtime_error);
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

// The derived exposure normalizes every environment to middle grey, so on its own no environment
// can be dimmer or brighter than another. The override is what says otherwise, and it has to
// survive both a round-trip and a re-bake -- a bake that refreshed the derivation and discarded the
// authored value would take a tuned environment back to the average with nothing to show for it.
TEST_CASE("an authored exposure survives a round-trip beside the derived one", "[benvl][io]")
{
	BEnvLighting lighting     = SampleLighting();
	lighting.exposure         = 1.25f;
	lighting.exposureOverride = 0.5f;

	const BEnvLighting restored = deserializeEnvLighting(serializeEnvLighting(lighting));

	CHECK(restored.exposure == Catch::Approx(1.25f));
	REQUIRE(restored.exposureOverride.has_value());
	CHECK(*restored.exposureOverride == Catch::Approx(0.5f));
	CHECK(restored.EffectiveExposure() == Catch::Approx(0.5f));
}

TEST_CASE("an unauthored exposure renders at what the bake derived", "[benvl][io]")
{
	BEnvLighting lighting = SampleLighting();
	lighting.exposure     = 1.25f;

	const BEnvLighting restored = deserializeEnvLighting(serializeEnvLighting(lighting));

	CHECK_FALSE(restored.exposureOverride.has_value());
	CHECK(restored.EffectiveExposure() == Catch::Approx(1.25f));
}
