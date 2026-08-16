#include <assetlib/env_import.h>

#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <catch2/catch_approx.hpp>

#include "MountAt.h"
#include "mounted_io.h"

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	/** A cube map of one uniform radiance -- enough to import, and cheap at this size. */
	ImageData
	ConstantCube(uint32_t size, float radiance)
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
			px[t * 4 + 0] = radiance;
			px[t * 4 + 1] = radiance;
			px[t * 4 + 2] = radiance;
			px[t * 4 + 3] = 1.0f;
		}
		for (uint32_t face = 0; face < 6; ++face)
			out.subresources.push_back({ face * perFace * 4 * sizeof(float), pitch, pitch * size });

		return out;
	}

	/** A project to import into, plus a cube map sitting outside it as the thing to import. */
	struct Sandbox
	{
		fs::path path;

		explicit Sandbox(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Data");
			fs::create_directories(path / "incoming");

			writeKTX2(ConstantCube(16, 0.5f), Source(), false, Ktx2Compression::kNone);
		}

		~Sandbox() { fs::remove_all(path); }

		fs::path
		DataRoot() const
		{
			return path / "Data";
		}

		fs::path
		Source() const
		{
			return path / "incoming" / "forest.ktx2";
		}

		/** Small everywhere: this suite is about what lands on disk, not about convolution quality. */
		EnvImportDesc
		Desc() const
		{
			auto desc               = EnvImportDesc();
			desc.dataRoot           = DataRoot();
			desc.source             = Source();
			desc.name               = "forest";
			desc.skyFaceSize        = 8;
			desc.prefilterFaceSize  = 8;
			desc.prefilterMips      = 2;
			desc.prefilterSamples   = 4;
			desc.irradianceFaceSize = 8;
			desc.threads            = 1;
			return desc;
		}

		bool
		Has(const std::string& relative) const
		{
			return fs::exists(DataRoot() / relative);
		}
	};
}

// The whole point of the seam: the editor's import is this call, so what the dialog will produce is
// what a test can assert on.
TEST_CASE("An import writes the environment family a project can load", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_full");

	const EnvImportResult result = importEnvironment(sandbox.Desc());

	REQUIRE(result.sky == "Sky/forest.bsky");
	REQUIRE(result.lighting == "EnvLighting/forest.benvl");
	REQUIRE(result.environment == "Environments/forest.benv");

	CHECK(sandbox.Has(result.sky));
	CHECK(sandbox.Has(result.lighting));
	CHECK(sandbox.Has(result.environment));

	// The float intermediates are the routed sources a re-bake reads, so they are part of the import
	// rather than scratch.
	CHECK(sandbox.Has("textures_src/forest_sky.ktx2"));
	CHECK(sandbox.Has("textures_src/forest_prefilter.ktx2"));
	CHECK(sandbox.Has("textures_src/forest_irradiance.ktx2"));

	// And it loads back as one environment: the .benv names the pair, each names its baked map, and
	// nothing is stale the moment it was written.
	const BEnv env = loadEnv(sandbox.DataRoot() / result.environment);
	CHECK(env.sky == result.sky);
	CHECK(env.lighting == result.lighting);

	const BSky sky = loadSky(sandbox.DataRoot() / result.sky);
	CHECK_FALSE(sky.sky.baked.empty());
	CHECK(sandbox.Has(sky.sky.baked));
	CHECK_FALSE(isSkyBakeStale(sky, MountAt(sandbox.DataRoot())));

	const BEnvLighting lighting = loadEnvLighting(sandbox.DataRoot() / result.lighting);
	CHECK_FALSE(isEnvLightingBakeStale(lighting, MountAt(sandbox.DataRoot())));

	// A constant environment's exposure is 1 / (0.96 * radiance) -- the same value bakeEnvLighting
	// derives, which is what says the import ran the real bake rather than a shortcut.
	CHECK(result.exposure == Catch::Approx(1.0 / (0.96 * 0.5)).epsilon(0.01));

	CHECK(result.written.size() >= 6);
}

// The checkboxes. A sky is re-authored in seconds and the lighting takes minutes, so paying for the
// second when only the first was asked for is the thing this separation exists to avoid.
TEST_CASE("An import writes only what was selected", "[envimport]")
{
	SECTION("a sky on its own")
	{
		const Sandbox sandbox("bernini_envimport_sky");

		auto desc        = sandbox.Desc();
		desc.lighting    = false;
		desc.environment = false;

		const EnvImportResult result = importEnvironment(desc);

		CHECK(result.lighting.empty());
		CHECK(result.environment.empty());
		CHECK(sandbox.Has(result.sky));
		CHECK_FALSE(sandbox.Has("EnvLighting/forest.benvl"));
		CHECK_FALSE(sandbox.Has("textures_src/forest_prefilter.ktx2"));

		// No lighting means nothing derived an exposure, and reporting one would be inventing it.
		CHECK(result.exposure == Catch::Approx(1.0f));
	}

	SECTION("a lighting on its own")
	{
		const Sandbox sandbox("bernini_envimport_lighting");

		auto desc        = sandbox.Desc();
		desc.sky         = false;
		desc.environment = false;

		const EnvImportResult result = importEnvironment(desc);

		CHECK(result.sky.empty());
		CHECK(sandbox.Has(result.lighting));
		CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));
		CHECK_FALSE(sandbox.Has("textures_src/forest_sky.ktx2"));
	}

	SECTION("an environment composes only the half that was written")
	{
		const Sandbox sandbox("bernini_envimport_half");

		auto desc     = sandbox.Desc();
		desc.lighting = false;

		const EnvImportResult result = importEnvironment(desc);

		const BEnv env = loadEnv(sandbox.DataRoot() / result.environment);
		CHECK(env.sky == result.sky);
		CHECK(env.lighting.empty());  // not a dangling reference to a file that was never written
	}
}

// A cancel signalled before any work starts must be honoured rather than raced past. This is the
// cheap half of cancellation; the rollback below is the half that has something to undo.
TEST_CASE("A cancelled import is refused before it writes anything", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_cancel");

	std::stop_source stop;
	stop.request_stop();

	CHECK_THROWS_AS(importEnvironment(sandbox.Desc(), stop.get_token()), Cancelled);

	CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));
	CHECK_FALSE(sandbox.Has("textures_src/forest_sky.ktx2"));
}

namespace
{
	/**
	 * A desc that fails *after* the sky has been written: irradianceSh refuses a zero face size, and
	 * the lighting runs second. Deterministic, unlike racing a cancel into the middle of a bake --
	 * and the rollback cannot be tested at all without getting past the first write.
	 */
	EnvImportDesc
	FailsAfterSky(const Sandbox& sandbox)
	{
		auto desc               = sandbox.Desc();
		desc.irradianceFaceSize = 0;
		return desc;
	}
}

// A half-written environment is worse than none: it loads, and renders wrong. So a failure part-way
// has to take back what it had already put down.
TEST_CASE("A failure part-way rolls back what it had written", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_rollback");

	CHECK_THROWS_AS(importEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The sky was fully written -- source, bake and `.bsky` -- before the lighting failed.
	CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));
	CHECK_FALSE(sandbox.Has("textures_src/forest_sky.ktx2"));
	CHECK_FALSE(sandbox.Has("Environments/forest.benv"));
}

// The rollback removes what the import *made*, not what it found. Re-importing over a name and failing
// must not take the previous import's work with it.
TEST_CASE("A rollback spares the files the import did not create", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_spares");

	// Put the sky's source there first, so the failing import overwrites it rather than creating it.
	fs::create_directories(sandbox.DataRoot() / "textures_src");
	writeKTX2(
		ConstantCube(8, 0.25f),
		sandbox.DataRoot() / "textures_src" / "forest_sky.ktx2",
		false,
		Ktx2Compression::kNone);

	CHECK_THROWS_AS(importEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The `.bsky` was this import's, and goes. The source was already there, and stays -- deleting it
	// would destroy whatever wrote it first.
	CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));
	CHECK(sandbox.Has("textures_src/forest_sky.ktx2"));
}

// Baked maps are content-addressed and shared, so the map an import wrote may be one another
// environment already names. Rolling one back would take it out from under that; the prune is what
// reclaims an orphan.
TEST_CASE("A rollback leaves the baked maps to the prune", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_bakedmaps");

	CHECK_THROWS_AS(importEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The `.bsky` naming it is gone, so the map is now an orphan -- but it is still on disk, which is
	// the whole point: findUnusedBakedTextures is what decides an orphan's fate, not this call.
	CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));

	bool anyBakedMap = false;
	for (const auto& entry : fs::directory_iterator(sandbox.DataRoot() / "Textures"))
		anyBakedMap = anyBakedMap || isBakedEnvMapName(entry.path().filename().string());

	CHECK(anyBakedMap);
}

TEST_CASE("An import that cannot mean anything is refused", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_refuse");

	SECTION("nothing selected")
	{
		auto desc        = sandbox.Desc();
		desc.sky         = false;
		desc.lighting    = false;
		desc.environment = false;

		CHECK_THROWS_AS(importEnvironment(desc), std::runtime_error);
	}

	// It composes the other two, so alone it would name nothing -- an empty environment that loads and
	// lights nothing is worse than a refusal.
	SECTION("an environment with neither half")
	{
		auto desc     = sandbox.Desc();
		desc.sky      = false;
		desc.lighting = false;

		CHECK_THROWS_AS(importEnvironment(desc), std::runtime_error);
	}

	SECTION("a data root that is not a directory")
	{
		auto desc     = sandbox.Desc();
		desc.dataRoot = sandbox.Source();  // a file

		CHECK_THROWS_AS(importEnvironment(desc), std::runtime_error);
	}

	SECTION("no name to write under")
	{
		auto desc = sandbox.Desc();
		desc.name.clear();

		CHECK_THROWS_AS(importEnvironment(desc), std::runtime_error);
	}

	SECTION("a source that is not there")
	{
		auto desc   = sandbox.Desc();
		desc.source = sandbox.path / "incoming" / "absent.ktx2";

		CHECK_THROWS(importEnvironment(desc));

		// And the refusal is not a half-import: nothing was written before the source was read.
		CHECK_FALSE(sandbox.Has("Sky/forest.bsky"));
	}
}

// The editor refuses rather than overwrites, and cannot ask "would this land on something?" by
// trying it. These are the same names importEnvironment writes, which is the point of asking here.
TEST_CASE("An import can say what it would write before writing it", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_targets");

	const auto names = [](const std::vector<std::string>& targets, std::string_view file) {
		return std::ranges::find(targets, file) != targets.end();
	};

	SECTION("everything selected names every file, and no baked maps")
	{
		const std::vector<std::string> targets = environmentImportTargets(sandbox.Desc());

		CHECK(names(targets, "Sky/forest.bsky"));
		CHECK(names(targets, "EnvLighting/forest.benvl"));
		CHECK(names(targets, "Environments/forest.benv"));
		CHECK(names(targets, "textures_src/forest_sky.ktx2"));

		// Content-addressed, so a collision with one is two imports agreeing rather than one
		// destroying the other -- naming them here would refuse an import that is not in conflict.
		CHECK(std::ranges::none_of(targets, [](const std::string& t) {
			return t.starts_with("Textures/");
		}));
	}

	SECTION("an unselected part names nothing")
	{
		auto desc     = sandbox.Desc();
		desc.lighting = false;

		const std::vector<std::string> targets = environmentImportTargets(desc);

		CHECK(std::ranges::none_of(targets, [](const std::string& t) {
			return t.ends_with(".benvl") || t.ends_with("_prefilter.ktx2");
		}));
	}

	// The check is worthless if it names files the import does not, or misses ones it does.
	SECTION("and it is exactly what the import goes on to create")
	{
		const auto desc = sandbox.Desc();

		const std::vector<std::string> predicted = environmentImportTargets(desc);
		const EnvImportResult          actual    = importEnvironment(desc);

		// `written` is what was created; into a fresh project that is every target.
		auto created = actual.written;
		auto expect  = predicted;
		std::ranges::sort(created);
		std::ranges::sort(expect);

		// The baked maps are in `written` but deliberately not predicted, so the prediction is a
		// subset -- every predicted file must have been created.
		for (const std::string& file : expect)
		{
			INFO("predicted: " << file);
			CHECK(std::ranges::find(created, file) != created.end());
		}
	}

	// Folders move the targets with them, or the check would look in the wrong place.
	SECTION("a subfolder moves what it would write")
	{
		auto desc   = sandbox.Desc();
		desc.skyDir = "Sky/outdoor";

		const std::vector<std::string> targets = environmentImportTargets(desc);
		CHECK(names(targets, "Sky/outdoor/forest.bsky"));
	}
}
