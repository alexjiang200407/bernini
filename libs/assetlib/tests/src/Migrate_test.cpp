#include <algorithm>
#include <array>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/image_io.h>
#include <assetlib/migrate.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/magic.h>

#include "CacheTamper.h"
#include "ImportUnitGroup.h"
#include "RecordedProgress.h"
#include "SkinnedGltf.h"
#include "bmesh_texture.h"
#include <assetlib/progress.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <chrono>
#include <core/file/file.h>

#include "MountAt.h"
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <span>
#include <string_view>
#include <vector>

using namespace assetlib;
using Catch::Matchers::ContainsSubstring;

namespace
{
	struct Project
	{
		std::filesystem::path root;

		Project()
		{
			root = std::filesystem::temp_directory_path() / "assetlib_migrate_test";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(root / "Authored/Materials");
		}

		~Project() { std::filesystem::remove_all(root); }

		void
		Write(const std::filesystem::path& relative, std::span<const std::byte> bytes) const
		{
			std::ofstream out(root / relative, std::ios::binary);
			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}

		std::vector<std::byte>
		Read(const std::filesystem::path& relative) const
		{
			return core::file::read_file_bytes((root / relative).string());
		}
	};

	std::vector<std::byte>
	MaterialBytes(std::string_view name)
	{
		BMaterial material;
		material.name = std::string(name);
		return AssetCodec<BMaterial>::Serialize(material);
	}

	/** The same content spelled non-canonically: what a hand edit or a merge leaves behind. */
	std::vector<std::byte>
	Older()
	{
		constexpr std::string_view c_Older = R"({"shadingModel": "pbr", "name": "older"})";
		const auto                 data = std::as_bytes(std::span(c_Older.data(), c_Older.size()));
		return { data.begin(), data.end() };
	}
}

TEST_CASE(
	"migrate rewrites what is not current, leaves what is, and reports what it cannot read",
	"[migrate]")
{
	const Project project;
	project.Write("Authored/Materials/current.bmaterial", MaterialBytes("current"));
	project.Write("Authored/Materials/older.bmaterial", Older());
	const std::vector<std::byte> flat = { std::byte{ 'B' },
		                                  std::byte{ 'M' },
		                                  std::byte{ 'A' },
		                                  std::byte{ 'T' } };
	project.Write("Authored/Materials/flat.bmaterial", flat);  // a chunk-era stream, unreadable now
	project.Write("notes.txt", flat);                          // not a container at all

	SECTION("a dry run reports and writes nothing")
	{
		const auto before = project.Read("Authored/Materials/older.bmaterial");
		const auto report = AssetStore(project.root).Migrate(true);
		CHECK(report.Count(MigratedFile::Outcome::kUnchanged) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Authored/Materials/older.bmaterial") == before);
		CHECK(report.files.size() == 3);  // notes.txt was never a candidate
	}

	SECTION("a real run rewrites once, and the second run finds nothing to do")
	{
		const auto first = AssetStore(project.root).Migrate(false);
		CHECK(first.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(first.Count(MigratedFile::Outcome::kFailed) == 1);

		const auto rewritten = project.Read("Authored/Materials/older.bmaterial");
		CHECK(rewritten == MaterialBytes("older"));  // stamped current again
		CHECK(AssetCodec<BMaterial>::Deserialize(rewritten).name == "older");

		const auto second = AssetStore(project.root).Migrate(false);
		CHECK(second.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(second.Count(MigratedFile::Outcome::kUnchanged) == 2);
		CHECK(second.Count(MigratedFile::Outcome::kFailed) == 1);
		CHECK(project.Read("Authored/Materials/flat.bmaterial") == flat);  // never half-written
	}

	SECTION("the failure says which file, and why")
	{
		const auto report = AssetStore(project.root).Migrate(true);
		const auto failed =
			std::ranges::find(report.files, MigratedFile::Outcome::kFailed, &MigratedFile::outcome);
		REQUIRE(failed != report.files.end());
		CHECK(failed->path.filename() == "flat.bmaterial");
		CHECK_THAT(failed->message, ContainsSubstring("bmaterial:"));
	}
}

TEST_CASE("migrate refuses a root that is not a directory", "[migrate]")
{
	REQUIRE_THROWS_WITH(
		AssetStore(std::filesystem::temp_directory_path() / "no_such_project_dir").Migrate(true),
		ContainsSubstring("is not a directory"));
}

TEST_CASE("migrate regenerates a stale group on disk, once", "[migrate][regen]")
{
	const Project           project;
	const test::SkinnedGltf source("bernini_migrate_regen_gltf");
	test::ImportUnitGroup(project.root, source.PackGlb());

	const auto meshPath  = project.root / "Derived/Meshes/unit.bmesh";
	const auto bskelPath = project.root / "Derived/Skeletons/unit.bskel";
	const auto banimPath = project.root / "Derived/Animations/unit.banim";

	test::TamperHeaderByte(meshPath, test::c_TokenOffset);
	test::TamperHeaderByte(bskelPath, test::c_TokenOffset);
	test::TamperHeaderByte(banimPath, test::c_TokenOffset);

	SECTION("the replay: one run rewrites the group, the second finds nothing to do")
	{
		const auto first = AssetStore(project.root).Migrate(false);
		CHECK(first.Count(MigratedFile::Outcome::kRewritten) == 3);
		CHECK(first.Count(MigratedFile::Outcome::kFailed) == 0);

		// Written current: the loads that refused the tampered files read them plainly now.
		CHECK(LoadAt<BMesh>(meshPath).source.key == "Authored/Meshes/unit.glb");
		CHECK_FALSE(LoadAt<AnimationSet>(banimPath).clips.empty());

		const auto second = AssetStore(project.root).Migrate(false);
		CHECK(second.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(second.Count(MigratedFile::Outcome::kFailed) == 0);
	}

	SECTION("a stale group whose source is gone is a per-file failure, never a guess")
	{
		std::filesystem::remove(project.root / "Authored/Meshes/unit.glb");

		const auto report = AssetStore(project.root).Migrate(false);
		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 0);
		CHECK(report.Count(MigratedFile::Outcome::kFailed) == 3);
	}
}

TEST_CASE("a rebind reaches disk through migrate without a regeneration", "[migrate][regen]")
{
	const Project           project;
	const test::SkinnedGltf source("bernini_migrate_rebind_gltf");
	test::ImportUnitGroup(project.root, source.PackGlb());

	// The source is gone, so what follows cannot be a regeneration -- the document alone
	// carries the rebind onto the disk bytes.
	std::filesystem::remove(project.root / "Authored/Meshes/unit.glb");
	AssetStore(project.root)
		.RebindSubmeshInDocument(
			"Authored/Meshes/unit.glb",
			"body",
			"Authored/Materials/blue.bmaterial");

	const auto report = AssetStore(project.root).Migrate(false);
	CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);

	const BMesh mesh = StoreAt(project.root).Load<BMesh>("Derived/Meshes/unit.bmesh");
	REQUIRE(mesh.materials.size() == 1);
	CHECK(mesh.materials[0] == "Authored/Materials/blue.bmaterial");
}

TEST_CASE("migrate brings a material's bake current", "[migrate][bake]")
{
	const Project project;
	std::filesystem::create_directories(project.root / "Derived/SourceTextures");

	const auto source      = project.root / "Derived/SourceTextures/a.ktx2";
	const auto writeSource = [&source](std::array<uint8_t, 4> rgba) {
		std::vector<std::byte> pixels(16u * 16u * 4u);
		for (size_t t = 0; t < 16u * 16u; ++t)
			for (size_t c = 0; c < 4; ++c) pixels[t * 4 + c] = static_cast<std::byte>(rgba[c]);

		writeKTX2(rgba8ToImage(pixels, 16, 16), source, false, Ktx2Compression::kNone);
	};

	writeSource({ { 200, 100, 50, 255 } });

	BMaterial material;
	material.pbr.routes[0] = { "Derived/SourceTextures/a.ktx2", 0 };
	StoreAt(project.root).BakeMaterial(material);
	project.Write("Authored/Materials/m.bmaterial", AssetCodec<BMaterial>::Serialize(material));

	const std::string wasBaked = material.pbr.baseColorTexture;
	REQUIRE_FALSE(wasBaked.empty());

	// Edit the source, leaving the material naming a map its routes no longer produce. That is the
	// drift migrate exists to close, and it is what a project carries the first time a bake's naming
	// rule moves under it.
	writeSource({ { 20, 30, 40, 255 } });
	std::filesystem::last_write_time(
		source,
		std::filesystem::last_write_time(source) + std::chrono::seconds(5));

	SECTION("a dry run names the drift and encodes nothing")
	{
		const auto before = project.Read("Authored/Materials/m.bmaterial");
		const auto report = AssetStore(project.root).Migrate(true);

		CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 1);
		CHECK(project.Read("Authored/Materials/m.bmaterial") == before);

		// Exactly the one map the first bake wrote: a dry run that had composited would have left a
		// second beside it.
		const auto textures =
			std::filesystem::directory_iterator(project.root / "Derived/BakedTextures");
		CHECK(std::ranges::distance(textures) == 1);
	}

	SECTION("a real run re-bakes once, and the second run finds nothing to do")
	{
		CHECK(
			AssetStore(project.root).Migrate(false).Count(MigratedFile::Outcome::kRewritten) == 1);

		const BMaterial rebaked =
			StoreAt(project.root).Load<BMaterial>("Authored/Materials/m.bmaterial");
		CHECK(rebaked.pbr.baseColorTexture != wasBaked);
		CHECK(std::filesystem::exists(project.root / rebaked.pbr.baseColorTexture));

		// The map the material used to name is left for the prune, not deleted here: another
		// material may still hold it.
		CHECK(std::filesystem::exists(project.root / wasBaked));

		CHECK(
			AssetStore(project.root).Migrate(false).Count(MigratedFile::Outcome::kRewritten) == 0);
	}
}

TEST_CASE("migrate tells a delivered material from a broken one", "[migrate][bake]")
{
	// A delivered project: the triplet shipped, the sources it was baked from did not. There is
	// nothing to re-bake from, and reporting every material in it as a failure would bury the ones
	// that are real.
	const Project project;

	BMaterial material;
	material.pbr.routes[0]        = { "Derived/SourceTextures/gone.ktx2", 0 };
	material.pbr.baseColorTexture = "Derived/BakedTextures/basecolor_0123456789abcdef.ktx2";
	project.Write("Authored/Materials/m.bmaterial", AssetCodec<BMaterial>::Serialize(material));

	const auto report = AssetStore(project.root).Migrate(false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CHECK(report.Count(MigratedFile::Outcome::kRewritten) == 0);
	CHECK(report.Count(MigratedFile::Outcome::kUnchanged) == 1);

	SECTION("but one routing a source that is there and one that is not has actually broken")
	{
		std::filesystem::create_directories(project.root / "Derived/SourceTextures");
		std::vector<std::byte> pixels(16u * 16u * 4u, std::byte{ 0x80 });
		writeKTX2(
			rgba8ToImage(pixels, 16, 16),
			project.root / "Derived/SourceTextures/here.ktx2",
			false,
			Ktx2Compression::kNone);

		material.pbr.routes[1] = { "Derived/SourceTextures/here.ktx2", 1 };
		project.Write("Authored/Materials/m.bmaterial", AssetCodec<BMaterial>::Serialize(material));

		const auto broken = AssetStore(project.root).Migrate(false);
		CHECK(broken.Count(MigratedFile::Outcome::kFailed) == 1);

		const auto failed =
			std::ranges::find(broken.files, MigratedFile::Outcome::kFailed, &MigratedFile::outcome);
		REQUIRE(failed != broken.files.end());
		CHECK_THAT(failed->message, ContainsSubstring("Derived/SourceTextures/gone.ktx2"));
	}
}

// Migrate's resave walk fans out across the files already on disk, so two things have to survive
// the threads: what it writes, and what it reports. Every other case here imports one group, which
// gives each rank one file -- core::parallel_for then collapses to a single thread and none of this runs.
//
// Deliberately *not* asserting the rank order through the sink. Reports are emitted when an item is
// claimed, and core::parallel_for claims indices in order from one counter, so they come out in path order
// whether the ranks are batched or not -- an assertion on it passes with the barrier removed. The
// barrier is about when an item *finishes*, which the sink cannot see.
TEST_CASE("Migrate's threaded walk leaves a settled project untouched", "[migrate]")
{
	const test::SkinnedGltf source("assetlib_migrate_threads_gltf");

	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "assetlib_migrate_threads";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	// Two independent groups, so a rank holds more than one file and the threads interleave.
	const std::filesystem::path glb = source.PackGlb();
	test::ImportUnitGroup(root, glb, "Authored/Materials/red.bmaterial", 30.0f, {}, "one");
	test::ImportUnitGroup(root, glb, "Authored/Materials/red.bmaterial", 30.0f, {}, "two");

	auto before = std::map<std::string, std::vector<std::byte>>();
	for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
		if (entry.is_regular_file())
			before.emplace(
				entry.path().lexically_relative(root).generic_string(),
				core::file::read_file_bytes(entry.path().string()));

	test::RecordedProgress recorded;
	const MigrateReport    report = AssetStore(root).Migrate(/*dryRun*/ false, recorded.Sink());

	// The strongest thing available without a second, serial implementation to diff against: these
	// files are already what the current serializer writes, so a walk that raced -- two threads in
	// one buffer, a half-written file read back by the next rank -- reports a rewrite or a failure
	// where a correct one reports neither.
	for (const MigratedFile& file : report.files)
	{
		INFO("file: " << file.path.filename().string() << " -- " << file.message);
		CHECK(file.outcome == MigratedFile::Outcome::kUnchanged);
	}

	auto after = std::map<std::string, std::vector<std::byte>>();
	for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
		if (entry.is_regular_file())
			after.emplace(
				entry.path().lexically_relative(root).generic_string(),
				core::file::read_file_bytes(entry.path().string()));

	CHECK(after == before);

	// And the walk really did have two files in a rank to hand out, or the above proves nothing
	// about threading at all.
	const std::vector<test::RecordedStep> resaved = recorded.Of(ProgressPhase::kResaving);
	CHECK(test::RecordedProgress::CountOf(resaved, ".bmesh") == 2);
	CHECK(test::RecordedProgress::CountOf(resaved, ".banim") == 2);

	std::filesystem::remove_all(root);
}
