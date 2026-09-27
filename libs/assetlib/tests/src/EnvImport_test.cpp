
#include <algorithm>
#include <array>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_import.h>  // IWYU pragma: keep -- completes MigrateReport's MovedTexture
#include <assetlib/asset_refs.h>
#include <assetlib/container_info.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/envmap.h>
#include <assetlib/image_io.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib/pak.h>
#include <assetlib/progress.h>
#include <assetlib/reimport.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "MountAt.h"
#include "io/mounted_io.h"
#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib_structs/VkFormat.h>
#include <core/containers/fixed_buffer.h>

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

		AssetStore
		Store() const
		{
			return AssetStore(DataRoot());
		}

		/** Small everywhere: this suite is about what lands on disk, not about convolution quality. */
		EnvImportDesc
		Desc() const
		{
			auto desc                          = EnvImportDesc();
			desc.source                        = Source();
			desc.name                          = "forest";
			desc.parameters.skyFaceSize        = 8;
			desc.parameters.prefilterFaceSize  = 8;
			desc.parameters.prefilterMips      = 2;
			desc.parameters.prefilterSamples   = 4;
			desc.parameters.irradianceFaceSize = 8;
			desc.threads                       = 1;
			return desc;
		}

		bool
		Has(const std::string& relative) const
		{
			return fs::exists(DataRoot() / relative);
		}

		ImportDocument
		Document() const
		{
			return loadImportDocument(DataRoot() / "Authored/EnvSources/forest.bimport");
		}

		std::vector<std::byte>
		Bytes(const std::string& relative) const
		{
			return core::file::read_file_bytes((DataRoot() / relative).string());
		}
	};

	/**
	 * A 16x8 equirectangular Radiance file with a horizontal gradient, written flat rather than
	 * run-length encoded -- which the reader accepts -- so the fixture needs no encoder. The
	 * gradient is what makes a projection's size visible in the cube it produces.
	 */
	fs::path
	WriteGradientHdr(const fs::path& path, unsigned char exponent = 129)
	{
		constexpr int c_Width  = 16;
		constexpr int c_Height = 8;

		std::ofstream out(path, std::ios::binary);
		out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << c_Height << " +X " << c_Width << "\n";
		for (int y = 0; y < c_Height; ++y)
			for (int x = 0; x < c_Width; ++x)
			{
				// Never 2 in the first byte, which would read as the start of an encoded line.
				const auto mantissa                     = static_cast<unsigned char>(40 + x * 12);
				const std::array<unsigned char, 4> rgbe = {
					{ mantissa, static_cast<unsigned char>(30 + y * 20), mantissa, exponent }
				};
				out.write(
					reinterpret_cast<const char*>(rgbe.data()),
					static_cast<std::streamsize>(rgbe.size()));
			}
		return path;
	}

	std::string
	SkyOutput(const Sandbox& sandbox)
	{
		return importOutputKey(sandbox.Document().identity, AssetType::kSky);
	}

	std::string
	LightingOutput(const Sandbox& sandbox)
	{
		return importOutputKey(sandbox.Document().identity, AssetType::kEnvLighting);
	}

	std::vector<std::string>
	FamilyOutputs(const Sandbox& sandbox)
	{
		return { LightingOutput(sandbox), SkyOutput(sandbox) };
	}
}

// The whole point of the seam: the editor's import is this call, so what the dialog will produce is
// what a test can assert on.
TEST_CASE("An import writes the environment family a project can load", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_full");

	const EnvImportResult result = sandbox.Store().ImportEnvironment(sandbox.Desc());

	REQUIRE(result.sky == SkyOutput(sandbox));
	REQUIRE(result.lighting == LightingOutput(sandbox));
	REQUIRE(result.environment == "Authored/Environments/forest.benv");

	CHECK(sandbox.Has(result.sky));
	CHECK(sandbox.Has(result.lighting));
	CHECK(sandbox.Has(result.environment));

	// Nothing float is kept: every route names the copied source, and a re-bake cooks from it.
	CHECK_FALSE(sandbox.Has("Derived/SourceTextures"));

	// And it loads back as one environment: the .benv names the pair, each names its baked map, and
	// nothing is stale the moment it was written.
	const BEnv env = StoreAt(sandbox.DataRoot()).Load<BEnv>(result.environment);
	CHECK(env.sky == result.sky);
	CHECK(env.lighting == result.lighting);

	const BSky sky = StoreAt(sandbox.DataRoot()).Load<BSky>(result.sky);
	CHECK(sky.sky.source == result.source);
	CHECK_FALSE(sky.sky.baked.empty());
	CHECK(sandbox.Has(sky.sky.baked));
	CHECK_FALSE(isSkyBakeStale(sky, MountAt(sandbox.DataRoot())));

	const BEnvLighting lighting = StoreAt(sandbox.DataRoot()).Load<BEnvLighting>(result.lighting);
	CHECK(lighting.prefilter.source == result.source);
	CHECK(lighting.irradiance.source == result.source);
	CHECK_FALSE(isEnvLightingBakeStale(lighting, MountAt(sandbox.DataRoot())));

	// A constant environment's exposure is 1 / (0.96 * radiance) -- the same value bakeEnvLighting
	// derives, which is what says the import ran the real bake rather than a shortcut.
	CHECK(result.exposure == Catch::Approx(1.0 / (0.96 * 0.5)).epsilon(0.01));

	// Baked maps are content-addressed and shared, so what the import created is the rest.
	auto written = result.written;
	std::ranges::sort(written);
	CHECK(
		written == std::vector<std::string>{ "Authored/EnvSources/forest.bimport",
	                                         "Authored/EnvSources/forest.ktx2",
	                                         "Authored/Environments/forest.benv",
	                                         LightingOutput(sandbox),
	                                         SkyOutput(sandbox) });
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

		const EnvImportResult result = sandbox.Store().ImportEnvironment(desc);

		CHECK(result.lighting.empty());
		CHECK(result.environment.empty());
		CHECK(sandbox.Has(result.sky));
		CHECK(sandbox.Store().GetFiles().Enumerate("Derived/EnvLighting").empty());

		// No lighting means nothing derived an exposure, and reporting one would be inventing it.
		CHECK(result.exposure == Catch::Approx(1.0f));
	}

	SECTION("a lighting on its own")
	{
		const Sandbox sandbox("bernini_envimport_lighting");

		auto desc        = sandbox.Desc();
		desc.sky         = false;
		desc.environment = false;

		const EnvImportResult result = sandbox.Store().ImportEnvironment(desc);

		CHECK(result.sky.empty());
		CHECK(sandbox.Has(result.lighting));
		CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
	}

	SECTION("an environment composes only the half that was written")
	{
		const Sandbox sandbox("bernini_envimport_half");

		auto desc     = sandbox.Desc();
		desc.lighting = false;

		const EnvImportResult result = sandbox.Store().ImportEnvironment(desc);

		const BEnv env = StoreAt(sandbox.DataRoot()).Load<BEnv>(result.environment);
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

	CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(sandbox.Desc(), stop.get_token()), Cancelled);

	CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.ktx2"));
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
		auto desc                          = sandbox.Desc();
		desc.parameters.irradianceFaceSize = 0;
		return desc;
	}
}

// A half-written environment is worse than none: it loads, and renders wrong. So a failure part-way
// has to take back what it had already put down.
TEST_CASE("A failure part-way rolls back what it had written", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_rollback");

	CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The sky was fully written -- bake and `.bsky` -- before the lighting failed.
	CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
	CHECK_FALSE(sandbox.Has("Authored/Environments/forest.benv"));

	// The copy went in first and the document goes in last; a failure between them takes the first
	// and never writes the second.
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.ktx2"));
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.bimport"));
}

// The rollback removes what the import *made*, not what it found. Re-importing over a name and failing
// must not take the previous import's work with it.
TEST_CASE("A rollback spares the files the import did not create", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_spares");

	// Put the `.bsky` there first, so the failing import overwrites it rather than creating it.
	auto sky            = sandbox.Desc();
	sky.lighting        = false;
	sky.environment     = false;
	const auto existing = sandbox.Store().ImportEnvironment(sky);
	fs::remove(sandbox.DataRoot() / "Authored/EnvSources/forest.bimport");
	fs::remove(sandbox.DataRoot() / "Authored/EnvSources/forest.ktx2");

	CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The copy was this import's, and goes. The `.bsky` was already there, and stays -- deleting it
	// would destroy whatever wrote it first.
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.ktx2"));
	CHECK(sandbox.Has(existing.sky));
}

// Baked maps are content-addressed and shared, so the map an import wrote may be one another
// environment already names. Rolling one back would take it out from under that; the prune is what
// reclaims an orphan.
TEST_CASE("A rollback leaves the baked maps to the prune", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_bakedmaps");

	CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(FailsAfterSky(sandbox)), std::runtime_error);

	// The `.bsky` naming it is gone, so the map is now an orphan -- but it is still on disk, which is
	// the whole point: FindUnusedBakedTextures is what decides an orphan's fate, not this call.
	CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());

	bool anyBakedMap = false;
	for (const auto& entry : fs::directory_iterator(sandbox.DataRoot() / "Derived/BakedTextures"))
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

		CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(desc), std::runtime_error);
	}

	// It composes the other two, so alone it would name nothing -- an empty environment that loads and
	// lights nothing is worse than a refusal.
	SECTION("an environment with neither half")
	{
		auto desc     = sandbox.Desc();
		desc.sky      = false;
		desc.lighting = false;

		CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(desc), std::runtime_error);
	}

	SECTION("a data root that is not a directory")
	{
		// The store's constructor refuses it, which is earlier than the import could: before a
		// desc has been built, let alone a file written.
		CHECK_THROWS_AS(AssetStore(sandbox.Source()), std::runtime_error);
	}

	SECTION("no name to write under")
	{
		auto desc = sandbox.Desc();
		desc.name.clear();

		CHECK_THROWS_AS(sandbox.Store().ImportEnvironment(desc), std::runtime_error);
	}

	SECTION("a source that is not there")
	{
		auto desc   = sandbox.Desc();
		desc.source = sandbox.path / "incoming" / "absent.ktx2";

		CHECK_THROWS(sandbox.Store().ImportEnvironment(desc));

		// And the refusal is not a half-import: nothing was written before the source was read.
		CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
	}
}

// The editor refuses rather than overwrites, and cannot ask "would this land on something?" by
// trying it. These are the same names ImportEnvironment writes, which is the point of asking here.
TEST_CASE("An import can say what it would write before writing it", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_targets");

	const auto names = [](const std::vector<std::string>& targets, std::string_view file) {
		return std::ranges::find(targets, file) != targets.end();
	};

	SECTION("everything selected names every file, and no baked maps")
	{
		const std::vector<std::string> targets =
			sandbox.Store().EnvironmentImportTargets(sandbox.Desc());

		CHECK(names(targets, "Authored/EnvSources/forest.ktx2"));
		CHECK(names(targets, "Authored/EnvSources/forest.bimport"));
		CHECK(names(targets, "Authored/Environments/forest.benv"));

		// Content-addressed, so a collision with one is two imports agreeing rather than one
		// destroying the other -- naming them here would refuse an import that is not in conflict.
		CHECK(std::ranges::none_of(targets, [](const std::string& t) {
			return t.starts_with("Derived/BakedTextures/");
		}));
	}

	SECTION("an unselected part names nothing")
	{
		auto desc     = sandbox.Desc();
		desc.lighting = false;

		const std::vector<std::string> targets = sandbox.Store().EnvironmentImportTargets(desc);

		CHECK(std::ranges::none_of(targets, [](const std::string& t) {
			return t.ends_with(".benvl");
		}));
	}

	// The check is worthless if it names files the import does not, or misses ones it does.
	SECTION("and it is exactly what the import goes on to create")
	{
		const auto desc = sandbox.Desc();

		const std::vector<std::string> predicted = sandbox.Store().EnvironmentImportTargets(desc);
		const EnvImportResult          actual    = sandbox.Store().ImportEnvironment(desc);

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
		auto desc              = sandbox.Desc();
		desc.importedSourceDir = "Authored/EnvSources/outdoor";

		const std::vector<std::string> targets = sandbox.Store().EnvironmentImportTargets(desc);
		CHECK(names(targets, "Authored/EnvSources/outdoor/forest.ktx2"));
	}
}

// What makes the family producible: the file it came from is in the project, and a document beside it
// says how the family was made from it and which files that made.
TEST_CASE("An import copies its source and writes the document beside it", "[envimport][importdoc]")
{
	const Sandbox sandbox("bernini_envimport_document");
	const auto    desc = sandbox.Desc();

	const EnvImportResult result = sandbox.Store().ImportEnvironment(desc);

	REQUIRE(result.source == "Authored/EnvSources/forest.ktx2");
	REQUIRE(result.document == "Authored/EnvSources/forest.bimport");
	CHECK(sandbox.Bytes(result.source) == core::file::read_file_bytes(desc.source.string()));

	const ImportDocument document = sandbox.Document();
	CHECK(document.source == result.source);
	CHECK(document.environment == desc.parameters);
	CHECK(document.outputs == FamilyOutputs(sandbox));
	CHECK(document.envSourceStamp == stampOf(sandbox.DataRoot() / result.source));
	CHECK(document.envSourceBakeToken == c_EnvSourceBakeToken);
	CHECK(document.envSkyParametersHash != 0);
	CHECK(document.envLightingParametersHash != 0);

	// Authored, and so never something a re-import would put back over a person's edits.
	CHECK(std::ranges::find(document.outputs, result.environment) == document.outputs.end());

	CHECK(std::ranges::find(result.written, result.source) != result.written.end());
	CHECK(std::ranges::find(result.written, result.document) != result.written.end());
}

TEST_CASE("The import's targets name the copy and the document", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_targets");

	const std::vector<std::string> targets =
		sandbox.Store().EnvironmentImportTargets(sandbox.Desc());
	CHECK(std::ranges::find(targets, "Authored/EnvSources/forest.ktx2") != targets.end());
	CHECK(std::ranges::find(targets, "Authored/EnvSources/forest.bimport") != targets.end());
}

// `Reimport` finds environments by walking `Authored/EnvSources`, so a source anywhere else is one a
// fresh checkout can never produce a family from.
TEST_CASE("A source copied outside its category is refused before anything runs", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_misplaced");

	auto desc              = sandbox.Desc();
	desc.importedSourceDir = "Authored/Environments";
	CHECK_THROWS_WITH(
		sandbox.Store().ImportEnvironment(desc),
		Catch::Matchers::ContainsSubstring("Authored/EnvSources"));
	CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());

	desc                   = sandbox.Desc();
	desc.importedSourceDir = "Authored/EnvSources/outdoor";
	CHECK_NOTHROW(sandbox.Store().ImportEnvironment(desc));
	CHECK(sandbox.Has("Authored/EnvSources/outdoor/forest.bimport"));
}

TEST_CASE("A source that is neither .hdr nor .ktx2 is refused", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_extension");

	fs::copy_file(sandbox.Source(), sandbox.path / "incoming" / "forest.exr");
	auto desc   = sandbox.Desc();
	desc.source = sandbox.path / "incoming" / "forest.exr";

	CHECK_THROWS_WITH(
		sandbox.Store().ImportEnvironment(desc),
		Catch::Matchers::ContainsSubstring(".hdr"));
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.exr"));
}

TEST_CASE(
	"Legacy environment names migrate with their authored references once",
	"[envimport][migrate]")
{
	const Sandbox sandbox("bernini_envimport_names_migrate");
	const auto    store    = sandbox.Store();
	const auto    imported = store.ImportEnvironment(sandbox.Desc());
	const auto    graph    = AssetRefGraph::Scan(store);
	auto          plan     = RenamePlan();
	plan.subject           = { imported.document, imported.document };
	plan.outputs           = { { imported.sky, "Derived/Sky/forest.bsky" },
		                       { imported.lighting, "Derived/EnvLighting/forest.benvl" } };
	for (const auto& move : plan.outputs)
		for (const auto& edge : graph.ReferrersOf(move.from))
			if (isStoredRef(edge.kind))
				plan.referrers.push_back(edge);
	REQUIRE(store.RenameAsset(plan).status == RenameStatus::kRenamed);
	auto legacy     = store.Load<ImportDocument>(imported.document);
	legacy.identity = {};
	store.Save(legacy, imported.document);
	const auto before = sandbox.Bytes(imported.document);
	const auto dry    = store.Migrate(true);
	CHECK(dry.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(sandbox.Bytes(imported.document) == before);
	CHECK(sandbox.Has("Derived/Sky/forest.bsky"));
	const auto migrated = store.Migrate(false);
	REQUIRE(migrated.Count(MigratedFile::Outcome::kFailed) == 0);
	const auto document = sandbox.Document();
	CHECK(document.identity.id != 0);
	CHECK(document.identity.label == "forest.ktx2");
	const auto env = store.Load<BEnv>(imported.environment);
	CHECK(env.sky == SkyOutput(sandbox));
	CHECK(env.lighting == LightingOutput(sandbox));
	CHECK(sandbox.Has(env.sky));
	CHECK(sandbox.Has(env.lighting));
	CHECK_FALSE(sandbox.Has("Derived/Sky/forest.bsky"));
	CHECK_FALSE(sandbox.Has("Derived/EnvLighting/forest.benvl"));
	const auto authored = sandbox.Bytes(imported.document);
	const auto again    = store.Migrate(false);
	CHECK(again.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(again.Count(MigratedFile::Outcome::kRewritten) == 0);
	CHECK(sandbox.Bytes(imported.document) == authored);
}

// The recovery an environment without its derived files is given: import again from the copy the
// project already holds. Copying a file onto itself would truncate it first.
TEST_CASE("Re-importing from the copy in the project leaves the copy intact", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_fromcopy");
	static_cast<void>(sandbox.Store().ImportEnvironment(sandbox.Desc()));
	const std::vector<std::byte> before = sandbox.Bytes("Authored/EnvSources/forest.ktx2");

	auto desc   = sandbox.Desc();
	desc.source = sandbox.DataRoot() / "Authored/EnvSources/forest.ktx2";
	CHECK_THROWS(sandbox.Store().ImportEnvironment(desc));
	fs::remove_all(sandbox.DataRoot() / "Derived");
	CHECK(sandbox.Store().Reimport(false).GetFailedCount() == 0);

	CHECK(sandbox.Bytes("Authored/EnvSources/forest.ktx2") == before);
	CHECK(sandbox.Document().outputs == FamilyOutputs(sandbox));
}

TEST_CASE(
	"A second environment import refuses the occupied source without changing files",
	"[envimport]")
{
	const Sandbox sandbox("bernini_envimport_duplicate");
	const auto    result    = sandbox.Store().ImportEnvironment(sandbox.Desc());
	const auto    source    = sandbox.Bytes(result.source);
	const auto    document  = sandbox.Bytes(result.document);
	const auto    sky       = sandbox.Bytes(result.sky);
	const auto    lighting  = sandbox.Bytes(result.lighting);
	auto          desc      = sandbox.Desc();
	desc.lighting           = false;
	desc.parameters.skyMips = 2;
	CHECK_THROWS_WITH(
		sandbox.Store().ImportEnvironment(desc),
		Catch::Matchers::ContainsSubstring("already exists"));
	CHECK(sandbox.Bytes(result.source) == source);
	CHECK(sandbox.Bytes(result.document) == document);
	CHECK(sandbox.Bytes(result.sky) == sky);
	CHECK(sandbox.Bytes(result.lighting) == lighting);
}

TEST_CASE("Same-named environment sources own distinct generated outputs", "[envimport]")
{
	const Sandbox sandbox("bernini_envimport_same_name");
	auto          desc     = sandbox.Desc();
	desc.environment       = false;
	desc.importedSourceDir = "Authored/EnvSources/one";
	const auto first       = sandbox.Store().ImportEnvironment(desc);
	desc.importedSourceDir = "Authored/EnvSources/two";
	const auto second      = sandbox.Store().ImportEnvironment(desc);
	const auto a           = sandbox.Store().Load<ImportDocument>(first.document).identity;
	const auto b           = sandbox.Store().Load<ImportDocument>(second.document).identity;
	CHECK(a.label == "forest.ktx2");
	CHECK(b.label == a.label);
	CHECK(a.id != b.id);
	CHECK(first.sky != second.sky);
	CHECK(first.lighting != second.lighting);
	CHECK(sandbox.Store().Reimport(false).GetWrittenCount() == 0);
}

// A document records each part's parameters apart, which only means something if each part's pixels
// follow from its own parameters. The lighting used to be convolved from a cube sized by the sky.
TEST_CASE("The lighting's pixels do not depend on the sky's face size", "[envimport]")
{
	const auto importWithSky = [](const char* name, uint32_t skyFaceSize) {
		const Sandbox sandbox(name);
		auto          desc          = sandbox.Desc();
		desc.source                 = WriteGradientHdr(sandbox.path / "incoming" / "forest.hdr");
		desc.parameters.skyFaceSize = skyFaceSize;
		static_cast<void>(sandbox.Store().ImportEnvironment(desc));
		const BEnvLighting lighting =
			StoreAt(sandbox.DataRoot()).Load<BEnvLighting>(LightingOutput(sandbox));
		return std::pair{ sandbox.Bytes(lighting.prefilter.baked),
			              sandbox.Bytes(lighting.irradiance.baked) };
	};

	// 16 is the lighting's own projection size at a prefilter of 8, so the first shares the sky's
	// cube and the second projects its own: both have to be the same cube.
	CHECK(
		importWithSky("bernini_envimport_shared", 16) ==
		importWithSky("bernini_envimport_apart", 8));
}

namespace
{
	/** An environment imported from the gradient `.hdr`, so both parts project and convolve. */
	EnvImportResult
	ImportGradient(const Sandbox& sandbox)
	{
		auto desc   = sandbox.Desc();
		desc.source = WriteGradientHdr(sandbox.path / "incoming" / "forest.hdr");
		return sandbox.Store().ImportEnvironment(desc);
	}

	const ReimportedSource*
	Find(const ReimportReport& report, std::string_view source)
	{
		const auto it = std::ranges::find(report.sources, source, &ReimportedSource::source);
		return it == report.sources.end() ? nullptr : &*it;
	}

	/** A file's modification time in ticks -- a number, so a failing check can print it. */
	auto
	WrittenAt(const Sandbox& sandbox, const std::string& relative)
	{
		return fs::last_write_time(sandbox.DataRoot() / relative).time_since_epoch().count();
	}
}

// The feature's acceptance: a checkout that ignores `Derived/` gets its environment back from the
// source and the document alone, and gets exactly the files a fresh import would have written.
TEST_CASE("Reimport puts an absent environment back, byte for byte", "[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_full");
	static_cast<void>(ImportGradient(sandbox));

	auto before = std::vector<std::vector<std::byte>>();
	for (const std::string& output : FamilyOutputs(sandbox))
		before.push_back(sandbox.Bytes(output));
	const std::vector<std::byte> document    = sandbox.Bytes("Authored/EnvSources/forest.bimport");
	const std::vector<std::byte> environment = sandbox.Bytes("Authored/Environments/forest.benv");

	fs::remove_all(sandbox.DataRoot() / "Derived");

	size_t      steps  = 0;
	const auto  report = sandbox.Store().Reimport(false, [&](const ProgressEvent&) { ++steps; });
	const auto* entry  = Find(report, "Authored/EnvSources/forest.hdr");
	REQUIRE(entry != nullptr);
	CHECK(entry->message.empty());
	CHECK(entry->written == FamilyOutputs(sandbox));
	CHECK(steps == FamilyOutputs(sandbox).size());

	for (size_t i = 0; i < before.size(); ++i)
	{
		INFO(FamilyOutputs(sandbox)[i]);
		CHECK(sandbox.Bytes(FamilyOutputs(sandbox)[i]) == before[i]);
	}

	// What a person authored is neither an output nor rewritten.
	CHECK(sandbox.Bytes("Authored/EnvSources/forest.bimport") == document);
	CHECK(sandbox.Bytes("Authored/Environments/forest.benv") == environment);

	// And the maps the containers name were baked on the way.
	const BSky sky = StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox));
	CHECK(sandbox.Has(sky.sky.baked));

	SECTION("a second run finds nothing to do")
	{
		CHECK(sandbox.Store().Reimport(false).GetWrittenCount() == 0);
	}
}

// Producing only what is missing is the point of splitting the parts: a lost sky is seconds of
// projection, and must not cost the lighting's minutes of convolution.
TEST_CASE("A lost container is re-cooked alone", "[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_container");
	static_cast<void>(ImportGradient(sandbox));

	const auto                   lightingAt = WrittenAt(sandbox, LightingOutput(sandbox));
	const std::vector<std::byte> sky        = sandbox.Bytes(SkyOutput(sandbox));
	fs::remove(sandbox.DataRoot() / SkyOutput(sandbox));

	const ReimportReport report = sandbox.Store().Reimport(false);
	const auto*          entry  = Find(report, "Authored/EnvSources/forest.hdr");
	REQUIRE(entry != nullptr);
	CHECK(entry->written == std::vector<std::string>{ SkyOutput(sandbox) });

	CHECK(sandbox.Bytes(SkyOutput(sandbox)) == sky);
	CHECK(WrittenAt(sandbox, LightingOutput(sandbox)) == lightingAt);
}

// No float source is left to draw instead, so a baked map lost under a container that is still there
// would leave the environment unloadable. Migrate re-bakes it, as it re-bakes a material's triplet.
TEST_CASE("Migrate re-bakes a map lost from under its container", "[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_bakedmap");
	static_cast<void>(ImportGradient(sandbox));

	const BSky                   sky   = StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox));
	const std::vector<std::byte> baked = sandbox.Bytes(sky.sky.baked);
	fs::remove(sandbox.DataRoot() / sky.sky.baked);
	REQUIRE(isSkyBakeStale(sky, MountAt(sandbox.DataRoot())));

	// The preview names it without cooking it, and writes nothing.
	const MigrateReport dry = sandbox.Store().Migrate(true);
	CHECK(std::ranges::any_of(dry.files, [&](const MigratedFile& file) {
		return file.path == sandbox.DataRoot() / SkyOutput(sandbox) &&
		       file.outcome == MigratedFile::Outcome::kRewritten;
	}));
	CHECK_FALSE(sandbox.Has(sky.sky.baked));

	const MigrateReport report = sandbox.Store().Migrate(false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(sandbox.Bytes(sky.sky.baked) == baked);
	CHECK_FALSE(isSkyBakeStale(
		StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox)),
		MountAt(sandbox.DataRoot())));
}

TEST_CASE("A dry run names an absent environment's files and writes none", "[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_dry");
	static_cast<void>(ImportGradient(sandbox));
	fs::remove_all(sandbox.DataRoot() / "Derived");

	const ReimportReport report = sandbox.Store().Reimport(true);
	const auto*          entry  = Find(report, "Authored/EnvSources/forest.hdr");
	REQUIRE(entry != nullptr);
	CHECK(entry->written == FamilyOutputs(sandbox));
	CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
}

TEST_CASE(
	"An environment Reimport cannot produce is reported, not guessed",
	"[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_broken");
	static_cast<void>(ImportGradient(sandbox));
	fs::remove_all(sandbox.DataRoot() / "Derived");

	SECTION("its source is gone")
	{
		fs::remove(sandbox.DataRoot() / "Authored/EnvSources/forest.hdr");

		const ReimportReport report = sandbox.Store().Reimport(false);
		const auto*          entry  = Find(report, "Authored/EnvSources/forest.hdr");
		REQUIRE(entry != nullptr);
		CHECK_THAT(entry->message, Catch::Matchers::ContainsSubstring("not in the project"));
		CHECK(entry->written.empty());
	}

	SECTION("its document claims a file no environment import writes")
	{
		ImportDocument document = sandbox.Document();
		document.outputs.push_back("Derived/SourceTextures/forest_extra.ktx2");
		StoreAt(sandbox.DataRoot()).Save(document, "Authored/EnvSources/forest.bimport");

		const ReimportReport report = sandbox.Store().Reimport(false);
		const auto*          entry  = Find(report, "Authored/EnvSources/forest.hdr");
		REQUIRE(entry != nullptr);
		CHECK_THAT(entry->message, Catch::Matchers::ContainsSubstring("forest_extra.ktx2"));
		CHECK(sandbox.Store().GetFiles().Enumerate("Derived/Sky").empty());
	}
}

namespace
{
	void
	EditDocument(const Sandbox& sandbox, const std::function<void(ImportDocument&)>& edit)
	{
		ImportDocument document = sandbox.Document();
		edit(document);
		StoreAt(sandbox.DataRoot()).Save(document, "Authored/EnvSources/forest.bimport");
	}

	/** The map a part's container names, decoded. */
	ImageData
	BakedSky(const Sandbox& sandbox)
	{
		const BSky sky = StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox));
		return loadKTX2(sandbox.DataRoot() / sky.sky.baked);
	}

	std::vector<std::byte>
	BakedPrefilter(const Sandbox& sandbox)
	{
		const BEnvLighting lighting =
			StoreAt(sandbox.DataRoot()).Load<BEnvLighting>(LightingOutput(sandbox));
		return sandbox.Bytes(lighting.prefilter.baked);
	}

	std::vector<decltype(WrittenAt(std::declval<const Sandbox&>(), ""))>
	WrittenAll(const Sandbox& sandbox, const std::vector<std::string>& files)
	{
		auto out = std::vector<decltype(WrittenAt(sandbox, ""))>();
		for (const std::string& file : files) out.push_back(WrittenAt(sandbox, file));
		return out;
	}

	const std::vector<std::string> c_Stale = { "Authored/EnvSources/forest.hdr" };
}

TEST_CASE("A freshly imported environment is current", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_fresh");
	static_cast<void>(ImportGradient(sandbox));

	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
	CHECK(sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr").empty());
}

// The split exists so the sky can be re-shaped without paying for the lighting, and that has to hold
// for an edited document as much as for a re-import.
TEST_CASE("An edited sky parameter re-cooks the sky alone", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_sky");
	static_cast<void>(ImportGradient(sandbox));
	const ImportDocument before = sandbox.Document();
	const auto           lightingAt =
		WrittenAll(sandbox, std::vector<std::string>{ LightingOutput(sandbox) });

	EditDocument(sandbox, [](ImportDocument& document) { document.environment->skyMips = 2; });
	REQUIRE(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	CHECK(
		sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr") ==
		std::vector<std::string>{ SkyOutput(sandbox) });
	CHECK(BakedSky(sandbox).mipLevels == 2);
	CHECK(WrittenAll(sandbox, std::vector<std::string>{ LightingOutput(sandbox) }) == lightingAt);

	const ImportDocument after = sandbox.Document();
	CHECK(after.envSkyParametersHash != before.envSkyParametersHash);
	CHECK(after.envLightingParametersHash == before.envLightingParametersHash);
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());

	// The container was re-baked at the new parameters, so it is current against its source.
	const BSky sky = StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox));
	CHECK_FALSE(isSkyBakeStale(sky, MountAt(sandbox.DataRoot())));
}

TEST_CASE("An edited lighting parameter re-cooks the lighting alone", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_lighting");
	static_cast<void>(ImportGradient(sandbox));
	const auto skyAt = WrittenAll(sandbox, std::vector<std::string>{ SkyOutput(sandbox) });

	EditDocument(sandbox, [](ImportDocument& document) {
		document.environment->prefilterSamples = 8;
	});
	REQUIRE(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	CHECK(
		sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr") ==
		std::vector<std::string>{ LightingOutput(sandbox) });
	CHECK(WrittenAll(sandbox, std::vector<std::string>{ SkyOutput(sandbox) }) == skyAt);
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// The source is what every part was cooked from, so a new file under the same name stales them all.
TEST_CASE("A source re-exported in place re-cooks every part", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_source");
	static_cast<void>(ImportGradient(sandbox));
	const std::vector<std::byte> prefilter = BakedPrefilter(sandbox);

	WriteGradientHdr(sandbox.DataRoot() / "Authored/EnvSources/forest.hdr", 131);
	REQUIRE(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	CHECK(
		sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr") ==
		FamilyOutputs(sandbox));
	CHECK(BakedPrefilter(sandbox) != prefilter);
	CHECK(
		sandbox.Document().envSourceStamp ==
		stampOf(sandbox.DataRoot() / "Authored/EnvSources/forest.hdr"));
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// A baked map carries no header, so the revision of the code that wrote it lives in the document.
TEST_CASE("A moved revision re-cooks every part", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_token");
	static_cast<void>(ImportGradient(sandbox));

	EditDocument(sandbox, [](ImportDocument& document) { document.envSourceBakeToken = 1; });
	REQUIRE(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	CHECK(
		sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr") ==
		FamilyOutputs(sandbox));
	CHECK(sandbox.Document().envSourceBakeToken == c_EnvSourceBakeToken);
}

TEST_CASE("An environment whose source is gone is not stale", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_nosource");
	static_cast<void>(ImportGradient(sandbox));

	EditDocument(sandbox, [](ImportDocument& document) { document.environment->skyMips = 2; });
	fs::remove(sandbox.DataRoot() / "Authored/EnvSources/forest.hdr");

	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// A part the document does not claim was never produced, so no parameter of it can stale anything.
TEST_CASE("An unclaimed part's parameters stale nothing", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_unclaimed");
	auto          desc = sandbox.Desc();
	desc.source        = WriteGradientHdr(sandbox.path / "incoming" / "forest.hdr");
	desc.lighting      = false;
	static_cast<void>(sandbox.Store().ImportEnvironment(desc));

	EditDocument(sandbox, [](ImportDocument& document) {
		document.environment->prefilterSamples = 8;
	});
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

TEST_CASE("Migrate re-cooks a stale environment; a dry run only names it", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_migrate");
	static_cast<void>(ImportGradient(sandbox));
	EditDocument(sandbox, [](ImportDocument& document) { document.environment->skyMips = 2; });

	const auto skyAt   = WrittenAll(sandbox, std::vector<std::string>{ SkyOutput(sandbox) });
	const auto hasPath = [&](const MigrateReport& report, const std::string& relative) {
		return std::ranges::any_of(report.files, [&](const MigratedFile& file) {
			return file.path == sandbox.DataRoot() / relative &&
			       file.outcome == MigratedFile::Outcome::kRewritten;
		});
	};

	const MigrateReport dry = sandbox.Store().Migrate(true);
	CHECK(hasPath(dry, "Authored/EnvSources/forest.bimport"));
	CHECK(WrittenAll(sandbox, std::vector<std::string>{ SkyOutput(sandbox) }) == skyAt);
	CHECK(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	const MigrateReport wet = sandbox.Store().Migrate(false);
	CHECK(wet.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(hasPath(wet, SkyOutput(sandbox)));
	CHECK_FALSE(sandbox.Has("Derived/SourceTextures"));
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// The document is committed and the container is not, so a checkout that pulls a document another
// machine re-cooked holds a current document over a container at the old revision. Nothing else
// rebuilds it: Reimport produces only what is absent, and the re-save walk cannot read it.
TEST_CASE("A container written at another revision re-cooks its part alone", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_revision");
	static_cast<void>(ImportGradient(sandbox));
	const auto lightingAt =
		WrittenAll(sandbox, std::vector<std::string>{ LightingOutput(sandbox) });

	// The header's bake token, eight bytes after the magic and the header version.
	std::vector<std::byte> sky = sandbox.Bytes(SkyOutput(sandbox));
	REQUIRE(sky.size() > 16);
	sky[8] = static_cast<std::byte>(static_cast<uint8_t>(sky[8]) ^ 0xffu);
	core::file::write_atomic(sandbox.DataRoot() / SkyOutput(sandbox), sky);
	REQUIRE_THROWS(StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox)));

	REQUIRE(sandbox.Store().GetStaleEnvironmentSources() == c_Stale);

	const MigrateReport report = sandbox.Store().Migrate(false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK_NOTHROW(StoreAt(sandbox.DataRoot()).Load<BSky>(SkyOutput(sandbox)));
	CHECK(WrittenAll(sandbox, std::vector<std::string>{ LightingOutput(sandbox) }) == lightingAt);
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// Reimport would convolve a missing part at the edited parameters, and the refresh would then convolve
// it whole again: minutes, twice. The refresh runs first, so `Reimport` finds nothing left to write.
TEST_CASE("Migrate cooks a part both absent and stale once", "[envimport][stale]")
{
	const Sandbox sandbox("bernini_envstale_absentstale");
	static_cast<void>(ImportGradient(sandbox));
	EditDocument(sandbox, [](ImportDocument& document) {
		document.environment->prefilterSamples = 8;
	});
	fs::remove(sandbox.DataRoot() / LightingOutput(sandbox));

	const MigrateReport report = sandbox.Store().Migrate(false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	for (const std::string& output : std::vector<std::string>{ LightingOutput(sandbox) })
	{
		INFO(output);
		// The walk reports every container it reads; only a rewrite is a cook.
		CHECK(std::ranges::count_if(report.files, [&](const MigratedFile& file) {
				  return file.path == sandbox.DataRoot() / output &&
			             file.outcome == MigratedFile::Outcome::kRewritten;
			  }) == 1);
	}
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}

// `ReimportedSource::written` promises every file that landed before a failure. A sky written and then a
// lighting that throws is the case that promise is for.
TEST_CASE("A part written before a later one throws is still reported", "[envimport][reimport]")
{
	const Sandbox sandbox("bernini_envreimport_partial");
	static_cast<void>(ImportGradient(sandbox));

	// Eight texels a side cannot carry five mips, so the lighting refuses after the sky is already on
	// disk.
	EditDocument(sandbox, [](ImportDocument& document) {
		document.environment->prefilterMips = 5;
	});
	fs::remove(sandbox.DataRoot() / SkyOutput(sandbox));
	fs::remove(sandbox.DataRoot() / LightingOutput(sandbox));

	const ReimportReport report = sandbox.Store().Reimport(false);
	const auto*          entry  = Find(report, "Authored/EnvSources/forest.hdr");
	REQUIRE(entry != nullptr);
	CHECK_THAT(entry->message, Catch::Matchers::ContainsSubstring("mips"));
	CHECK(entry->written == std::vector<std::string>{ SkyOutput(sandbox) });
	CHECK(sandbox.Has(SkyOutput(sandbox)));
}

// A project an older build imported claims the float cubes it wrote beside each container. Those are
// nobody's output now, and a document that still named them would have Reimport try to produce them.
TEST_CASE("A document claiming the old float cubes reads without them", "[envimport][importdoc]")
{
	const Sandbox sandbox("bernini_envimport_retired");
	static_cast<void>(ImportGradient(sandbox));

	EditDocument(sandbox, [](ImportDocument& document) {
		document.outputs.push_back("Derived/SourceTextures/forest_sky.ktx2");
		document.outputs.push_back("Derived/SourceTextures/forest_prefilter.ktx2");
		document.outputs.push_back("Derived/SourceTextures/forest_irradiance.ktx2");
		document.envSourceBakeToken = 1;
	});
	CHECK(sandbox.Document().outputs == FamilyOutputs(sandbox));

	CHECK(
		sandbox.Store().RefreshEnvironmentSource("Authored/EnvSources/forest.hdr") ==
		FamilyOutputs(sandbox));
	CHECK(sandbox.Store().Reimport(false).GetWrittenCount() == 0);
	CHECK_FALSE(sandbox.Has("Derived/SourceTextures"));

	const std::vector<std::byte> bytes = sandbox.Bytes("Authored/EnvSources/forest.bimport");
	const auto text = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	CHECK(text.find("_sky.ktx2") == std::string::npos);
}

namespace
{
	RenameResult
	RenameIn(const Sandbox& sandbox, std::string_view from, std::string_view to)
	{
		const AssetStore store = sandbox.Store();
		return store.RenameAsset(planRename(AssetRefGraph::Scan(store), from, to));
	}

	DeletionResult
	DeleteIn(const Sandbox& sandbox, std::string_view target)
	{
		const AssetStore store = sandbox.Store();
		return store.DeleteAsset(planCascadeDeletion(AssetRefGraph::Scan(store), target));
	}

	void
	CheckRenamedToDusk(const Sandbox& sandbox)
	{
		CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.bimport"));

		const ImportDocument document =
			loadImportDocument(sandbox.DataRoot() / "Authored/EnvSources/dusk.bimport");
		CHECK(
			document.outputs ==
			std::vector<std::string>{ importOutputKey(document.identity, AssetType::kEnvLighting),
		                              importOutputKey(document.identity, AssetType::kSky) });

		// Everything that names the family follows it: the authored `.benv`, and the containers'
		// routes into the source.
		const BEnv env =
			StoreAt(sandbox.DataRoot()).Load<BEnv>("Authored/Environments/forest.benv");
		CHECK(env.sky == importOutputKey(document.identity, AssetType::kSky));
		CHECK(env.lighting == importOutputKey(document.identity, AssetType::kEnvLighting));
		CHECK(
			StoreAt(sandbox.DataRoot())
				.Load<BSky>(importOutputKey(document.identity, AssetType::kSky))
				.sky.source == "Authored/EnvSources/" + document.identity.label);
		CHECK(
			StoreAt(sandbox.DataRoot())
				.Load<BEnvLighting>(importOutputKey(document.identity, AssetType::kEnvLighting))
				.prefilter.source == "Authored/EnvSources/" + document.identity.label);

		const AssetStore store = sandbox.Store();
		CHECK(AssetRefGraph::Scan(store).broken.empty());
		CHECK(store.GetStaleEnvironmentSources().empty());
	}
}

TEST_CASE("Renaming an environment source preserves derived bytes", "[envimport][assetrename]")
{
	const Sandbox sandbox("bernini_envrename_source");
	static_cast<void>(ImportGradient(sandbox));
	const auto store         = sandbox.Store();
	const auto skyKey        = SkyOutput(sandbox);
	const auto lightingKey   = LightingOutput(sandbox);
	const auto skyBytes      = store.GetFiles().Read(skyKey);
	const auto lightingBytes = store.GetFiles().Read(lightingKey);

	REQUIRE(
		RenameIn(sandbox, "Authored/EnvSources/forest.hdr", "Authored/EnvSources/dusk.hdr")
			.status == RenameStatus::kRenamed);
	CHECK(sandbox.Has("Authored/EnvSources/dusk.hdr"));
	CHECK(
		loadImportDocument(sandbox.DataRoot() / "Authored/EnvSources/dusk.bimport").source ==
		"Authored/EnvSources/dusk.hdr");
	CheckRenamedToDusk(sandbox);
	CHECK(store.GetFiles().Read(skyKey) == skyBytes);
	CHECK(store.GetFiles().Read(lightingKey) == lightingBytes);
	CHECK(store.Migrate(false).Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(store.GetFiles().Read(skyKey) == skyBytes);
	CHECK(store.GetFiles().Read(lightingKey) == lightingBytes);
	CHECK(store.Pack(PackDesc{ sandbox.DataRoot() / "moved.bpak" }).envsRebaked == 0);
	CHECK(store.GetFiles().Read(skyKey) == skyBytes);
	CHECK(store.GetFiles().Read(lightingKey) == lightingBytes);

	SECTION("missing maps regenerate through the moved sidecar")
	{
		fs::remove(sandbox.DataRoot() / store.Load<BSky>(skyKey).sky.baked);
		CHECK(store.Migrate(false).Count(MigratedFile::Outcome::kFailed) == 0);
		CHECK(store.Exists(store.Load<BSky>(skyKey).sky.baked));
	}

	SECTION("and the renamed document still produces what it claims")
	{
		fs::remove_all(sandbox.DataRoot() / "Derived");
		const ReimportReport report = sandbox.Store().Reimport(false);
		const auto*          entry  = Find(report, "Authored/EnvSources/dusk.hdr");
		REQUIRE(entry != nullptr);
		CHECK(entry->message.empty());
		CHECK(
			entry->written ==
			loadImportDocument(sandbox.DataRoot() / "Authored/EnvSources/dusk.bimport").outputs);
	}
}

TEST_CASE("Renaming an environment's document plans the same move", "[envimport][assetrename]")
{
	const Sandbox sandbox("bernini_envrename_document");
	static_cast<void>(ImportGradient(sandbox));

	REQUIRE(
		RenameIn(sandbox, "Authored/EnvSources/forest.bimport", "Authored/EnvSources/dusk.bimport")
			.status == RenameStatus::kRenamed);
	CHECK(sandbox.Has("Authored/EnvSources/dusk.hdr"));
	CheckRenamedToDusk(sandbox);
}

// A `.ktx2` is otherwise a texture, and renaming one as a texture would move the source out from under
// its document. What makes it a source is the document naming it.
TEST_CASE("A cube source renames as a source, not as a texture", "[envimport][assetrename]")
{
	const Sandbox sandbox("bernini_envrename_cube");
	static_cast<void>(sandbox.Store().ImportEnvironment(sandbox.Desc()));

	REQUIRE(
		RenameIn(sandbox, "Authored/EnvSources/forest.ktx2", "Authored/EnvSources/dusk.ktx2")
			.status == RenameStatus::kRenamed);
	CHECK(sandbox.Has("Authored/EnvSources/dusk.bimport"));
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.bimport"));
	CheckRenamedToDusk(sandbox);
}

// The old rule derived a source's document from its path and could not disagree with itself; a
// search can, and moving whichever document sorted first would move the wrong import.
TEST_CASE("A source two documents claim is refused a rename", "[envimport][assetrename]")
{
	const Sandbox sandbox("bernini_envrename_ambiguous");
	static_cast<void>(ImportGradient(sandbox));

	ImportDocument impostor = sandbox.Document();
	impostor.outputs.clear();
	StoreAt(sandbox.DataRoot()).Save(impostor, "Authored/EnvSources/impostor.bimport");

	CHECK_THROWS_WITH(
		planRename(
			AssetRefGraph::Scan(sandbox.Store()),
			"Authored/EnvSources/forest.hdr",
			"Authored/EnvSources/dusk.hdr"),
		Catch::Matchers::ContainsSubstring("both record"));
}

TEST_CASE(
	"An environment source is refused a rename that would lose it",
	"[envimport][assetrename]")
{
	const Sandbox sandbox("bernini_envrename_refused");
	static_cast<void>(ImportGradient(sandbox));
	const AssetRefGraph graph = AssetRefGraph::Scan(sandbox.Store());

	SECTION("into another kind")
	{
		CHECK_THROWS_WITH(
			planRename(graph, "Authored/EnvSources/forest.hdr", "Authored/EnvSources/forest.ktx2"),
			Catch::Matchers::ContainsSubstring("kind of asset"));
	}

	// `Reimport` enumerates the category, so a source moved out of it is one nothing can produce the
	// environment from again.
	SECTION("out of its category, by the source, the document or the folder")
	{
		CHECK_THROWS_WITH(
			planRename(graph, "Authored/EnvSources/forest.hdr", "Authored/Environments/forest.hdr"),
			Catch::Matchers::ContainsSubstring("Authored/EnvSources"));
		CHECK_THROWS_WITH(
			planRename(
				graph,
				"Authored/EnvSources/forest.bimport",
				"Authored/Meshes/forest.bimport"),
			Catch::Matchers::ContainsSubstring("Authored/EnvSources"));

		auto desc              = sandbox.Desc();
		desc.name              = "valley";
		desc.importedSourceDir = "Authored/EnvSources/outdoor";
		static_cast<void>(sandbox.Store().ImportEnvironment(desc));
		const AssetRefGraph withFolder = AssetRefGraph::Scan(sandbox.Store());
		CHECK_THROWS_WITH(
			planRename(withFolder, "Authored/EnvSources/outdoor", "Authored/Levels/outdoor"),
			Catch::Matchers::ContainsSubstring("Authored/EnvSources"));
		CHECK_NOTHROW(
			planRename(withFolder, "Authored/EnvSources/outdoor", "Authored/EnvSources/valleys"));
	}
}

// The containers it produced stay, because a document's claim is not a reference -- and so does the
// source, because they route it: it is what their next bake cooks.
TEST_CASE(
	"Deleting an environment's document leaves the source its containers route",
	"[envimport][cascade]")
{
	const Sandbox sandbox("bernini_envdelete_document");
	static_cast<void>(ImportGradient(sandbox));
	const auto outputs = FamilyOutputs(sandbox);

	REQUIRE(
		DeleteIn(sandbox, "Authored/EnvSources/forest.bimport").status == DeletionStatus::kDeleted);
	CHECK_FALSE(sandbox.Has("Authored/EnvSources/forest.bimport"));
	CHECK(sandbox.Has("Authored/EnvSources/forest.hdr"));
	for (const std::string& output : outputs) CHECK(sandbox.Has(output));
}

TEST_CASE("An environment's source and parts are held by what names them", "[envimport][cascade]")
{
	const Sandbox sandbox("bernini_envdelete_held");
	static_cast<void>(ImportGradient(sandbox));

	CHECK_THROWS(DeleteIn(sandbox, "Authored/EnvSources/forest.hdr"));

	// A cube source is a texture by extension, so it reaches the plan -- and the document naming it
	// is what refuses it there.
	auto cube = sandbox.Desc();
	cube.name = "valley";
	static_cast<void>(sandbox.Store().ImportEnvironment(cube));
	CHECK(DeleteIn(sandbox, "Authored/EnvSources/valley.ktx2").status == DeletionStatus::kRefused);
	CHECK(sandbox.Has("Authored/EnvSources/valley.ktx2"));
	CHECK(DeleteIn(sandbox, SkyOutput(sandbox)).status == DeletionStatus::kRefused);
	for (const std::string& output : FamilyOutputs(sandbox)) CHECK(sandbox.Has(output));
}

// Deleting the environment a person authored frees what only it named. Each freed file's claim goes
// with it, or `Reimport` would read the claim as absent and put the file straight back.
TEST_CASE("Deleting the environment frees its parts and their claims", "[envimport][cascade]")
{
	const Sandbox sandbox("bernini_envdelete_benv");
	static_cast<void>(ImportGradient(sandbox));

	REQUIRE(
		DeleteIn(sandbox, "Authored/Environments/forest.benv").status == DeletionStatus::kDeleted);
	for (const std::string& output : FamilyOutputs(sandbox)) CHECK_FALSE(sandbox.Has(output));

	CHECK(sandbox.Document().outputs.empty());
	CHECK(sandbox.Store().Reimport(false).GetWrittenCount() == 0);
	CHECK(sandbox.Store().GetStaleEnvironmentSources().empty());
}
