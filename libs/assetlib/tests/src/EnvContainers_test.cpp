#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/magic.h>

#include <core/io/ByteWriter.h>

#include "MountAt.h"
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

	/** The scratch root these round trips write into -- per process since #470. */
	std::filesystem::path
	TempRoot()
	{
		return std::filesystem::temp_directory_path();
	}

	std::string
	TempKey(const char* name)
	{
		return std::string("bernini_") + name;
	}
}

TEST_CASE("a BSky survives a serialize round-trip", "[bsky][io]")
{
	const BSky sky      = SampleSky();
	const BSky restored = deserializeSky(serializeSky(sky));

	CHECK(restored.name == sky.name);
	CHECK(restored.sky == sky.sky);
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

	const BEnvLighting lighting = deserializeEnvLighting(serializeEnvLighting(BEnvLighting{}));
	CHECK(lighting.prefilter.source.empty());
	CHECK(lighting.irradiance.stamp == SourceStamp{});
	CHECK(lighting.exposure == Catch::Approx(1.0f));
}

TEST_CASE("the environment containers round-trip through a file", "[bsky][benvl][io]")
{
	const auto skyPath      = TempKey("env_container.bsky");
	const auto lightingPath = TempKey("env_container.benvl");

	StoreAt(TempRoot()).Save(SampleSky(), skyPath);
	StoreAt(TempRoot()).Save(SampleLighting(), lightingPath);

	CHECK(StoreAt(TempRoot()).Load<BSky>(skyPath).sky == SampleSky().sky);
	CHECK(StoreAt(TempRoot()).Load<BEnvLighting>(lightingPath).exposure == Catch::Approx(0.375f));

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
	env.name             = "forest";
	env.sky              = "Sky/forest.bsky";
	env.lighting         = "EnvLighting/forest.benvl";
	env.skyMipLevel      = 3;
	env.skyRotationY     = 1.25f;
	env.exposureOverride = 0.5f;

	const BEnv restored = deserializeEnv(serializeEnv(env));
	CHECK(restored.name == env.name);
	CHECK(restored.sky == env.sky);
	CHECK(restored.lighting == env.lighting);
	CHECK(restored.skyMipLevel == 3);
	CHECK(restored.skyRotationY == Catch::Approx(1.25f));
	REQUIRE(restored.exposureOverride.has_value());
	CHECK(*restored.exposureOverride == Catch::Approx(0.5f));

	// Half-composed is a legal file: the import's checkboxes write whichever pieces were asked for,
	// and what a .benv must reference is its consumer's rule, not the container's.
	const BEnv empty = deserializeEnv(serializeEnv(BEnv{}));
	CHECK(empty.name.empty());
	CHECK(empty.sky.empty());
	CHECK(empty.lighting.empty());
}

TEST_CASE("a BEnv round-trips through a file", "[benv][io]")
{
	const auto path = TempKey("env_container.benv");

	BEnv env;
	env.name     = "forest";
	env.sky      = "Sky/forest.bsky";
	env.lighting = "EnvLighting/forest.benvl";
	StoreAt(TempRoot()).Save(env, path);

	const BEnv restored = StoreAt(TempRoot()).Load<BEnv>(path);
	CHECK(restored.sky == env.sky);
	CHECK(restored.lighting == env.lighting);

	std::filesystem::remove(path);
}

// A chunk-era .benv -- any of its binary forms -- is not a text document, so the reader refuses
// it whole and never reads on into what follows the magic.
TEST_CASE(
	"the reference reader refuses a chunk-era .benv and the other containers' files",
	"[benv][io]")
{
	core::io::ByteWriter v1;
	v1.WritePod(magic::c_BEnv);
	v1.WritePod<uint16_t>(1);
	v1.WritePod<uint16_t>(0);
	v1.WritePod<uint64_t>(0);  // the old header continues; the reader must not get that far
	CHECK_THROWS_WITH(
		deserializeEnv(v1.Take()),
		Catch::Matchers::ContainsSubstring("no longer convertible"));

	CHECK_THROWS_AS(deserializeEnv(serializeSky(SampleSky())), std::runtime_error);
	CHECK_THROWS_AS(deserializeEnv(serializeEnvLighting(SampleLighting())), std::runtime_error);

	auto truncated = serializeEnv(BEnv{ .name = "forest", .sky = "Sky/forest.bsky" });
	truncated.resize(truncated.size() / 2);
	CHECK_THROWS_AS(deserializeEnv(truncated), std::runtime_error);
}

TEST_CASE(
	"an environment container at a header version this build does not read is refused",
	"[bsky][benvl][io]")
{
	const auto patchVersion = [](std::vector<std::byte>& bytes) {
		REQUIRE(bytes.size() > 8);
		const uint32_t version = 2;
		std::memcpy(bytes.data() + 4, &version, sizeof(version));
	};

	auto bytes = serializeSky(SampleSky());
	patchVersion(bytes);
	CHECK_THROWS_WITH(
		deserializeSky(bytes),
		Catch::Matchers::ContainsSubstring("not the 1 this build reads"));

	auto lightingBytes = serializeEnvLighting(SampleLighting());
	patchVersion(lightingBytes);
	CHECK_THROWS_AS(deserializeEnvLighting(lightingBytes), std::runtime_error);
}

// The authored exposure lives on the environment document, not the derived lighting: a decision a
// re-bake must never touch belongs to the file re-bakes never write. The derivation stays on the
// lighting, refreshed by every bake.
TEST_CASE("an unset override serializes as absent, not as a value", "[benv][io]")
{
	const BEnv restored = deserializeEnv(serializeEnv(BEnv{ .name = "plain" }));
	CHECK_FALSE(restored.exposureOverride.has_value());
	CHECK(restored.skyMipLevel == 0);
	CHECK(restored.skyRotationY == 0.0f);

	// Absent in the bytes too: an "exposureOverride": 0 that crept in would author a value.
	const auto        bytes = serializeEnv(BEnv{ .name = "plain" });
	const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	CHECK(text.find("exposureOverride") == std::string::npos);
}

TEST_CASE("a benv document preserves the keys this build does not know", "[benv][io]")
{
	constexpr std::string_view c_Text = R"({
	"name": "future",
	"weather": { "rain": 0.5 },
	"sky": "Sky/forest.bsky"
}
)";

	const BEnv env = deserializeEnv(std::as_bytes(std::span(c_Text.data(), c_Text.size())));
	CHECK(env.name == "future");
	CHECK(env.sky == "Sky/forest.bsky");

	const auto        resaved = serializeEnv(env);
	const std::string out(reinterpret_cast<const char*>(resaved.data()), resaved.size());
	CHECK(out.find("\"weather\"") != std::string::npos);
	CHECK(serializeEnv(deserializeEnv(resaved)) == resaved);
}
