#include <algorithm>
#include <assetlib/codecs.h>
#include <assetlib/reimport.h>

#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/asset_refs.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>
#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "ImportUnitGroup.h"
#include "MountAt.h"
#include "RecordedProgress.h"
#include "SkinnedGltf.h"
#include <assetlib/progress.h>

// Every other path that makes a container current is keyed on the file already being there --
// LoadRegen* peeks the header it was handed, Migrate walks the data root. Reimport is the one that
// runs the other way, from the sources and the outputs their documents name, so a project holding
// no derived files at all can be made whole. That is what lets them be gitignored.

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	struct ImportedProject
	{
		Project  project;
		fs::path dataRoot;

		explicit ImportedProject(const char* name, const fs::path& glb) : project(MakeProject(name))
		{
			dataRoot = project.GetDataDirectory();
			test::ImportUnitGroup(dataRoot, glb, "Authored/Materials/red.bmaterial", 30.0f);
			project.ReloadStore();
		}

		[[nodiscard]] const AssetStore&
		Store() const
		{
			return project.GetStore();
		}

	private:
		static Project
		MakeProject(const char* name)
		{
			const fs::path root = fs::temp_directory_path() / name;
			fs::remove_all(root);
			return Project::Create(root / "Reimport.bproj", "Reimport");
		}
	};

	/** Every derived container in the project, keyed and with the bytes it holds. */
	std::map<std::string, std::vector<std::byte>>
	DerivedFiles(const fs::path& dataRoot)
	{
		auto files = std::map<std::string, std::vector<std::byte>>();
		for (const auto& entry : fs::recursive_directory_iterator(dataRoot))
		{
			if (!entry.is_regular_file())
				continue;
			const auto type = assetTypeFromExtension(entry.path());
			if (type != AssetType::kMesh && type != AssetType::kSkeleton &&
			    type != AssetType::kAnimation)
				continue;

			files.emplace(
				entry.path().lexically_relative(dataRoot).generic_string(),
				core::file::read_file_bytes(entry.path().string()));
		}
		return files;
	}

	Project
	MakeTexProject()
	{
		const fs::path root = fs::temp_directory_path() / "bernini_reimport_tex";
		fs::remove_all(root);
		return Project::Create(root / "Reimport.bproj", "Reimport");
	}

	/** Per key, so a failure names the container that drifted rather than "three maps differ". */
	void
	CheckSameFiles(
		const std::map<std::string, std::vector<std::byte>>& got,
		const std::map<std::string, std::vector<std::byte>>& want)
	{
		auto gotKeys  = std::vector<std::string>();
		auto wantKeys = std::vector<std::string>();
		for (const auto& entry : got) gotKeys.push_back(entry.first);
		for (const auto& entry : want) wantKeys.push_back(entry.first);
		CHECK(gotKeys == wantKeys);

		for (const auto& [key, bytes] : want)
		{
			const auto found = got.find(key);
			if (found == got.end())
				continue;
			INFO("container: " << key);
			CHECK(found->second.size() == bytes.size());
			CHECK(found->second == bytes);
		}
	}
}

TEST_CASE("A project with no derived containers is rebuilt from its sources", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_gltf");
	const ImportedProject   project("bernini_reimport", source.PackGlb());

	const auto before = DerivedFiles(project.dataRoot);

	// The rig, the mesh and the clips: everything the import wrote, and everything a gitignore of
	// the derived tree would leave a fresh checkout without.
	REQUIRE(before.size() == 3);
	REQUIRE(before.contains("Derived/Meshes/unit.bmesh"));
	REQUIRE(before.contains("Derived/Skeletons/unit.bskel"));
	REQUIRE(before.contains("Derived/Animations/unit.banim"));

	for (const auto& entry : before) fs::remove(project.dataRoot / entry.first);

	const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
	REQUIRE(report.GetFailedCount() == 0);
	CHECK(report.GetWrittenCount() == 3);

	CheckSameFiles(DerivedFiles(project.dataRoot), before);

	// The box a skinned load reads: if the production had baked none, a load would fall back to
	// measuring one, which is the cost the bake exists to remove.
	{
		const AnimationSet clips = AssetCodec<AnimationSet>::Deserialize(
			core::file::read_file_bytes(
				(project.dataRoot / "Derived/Animations/unit.banim").string()));
		const BMesh mesh = AssetCodec<BMesh>::Deserialize(
			core::file::read_file_bytes((project.dataRoot / "Derived/Meshes/unit.bmesh").string()));
		const Skeleton rig = AssetCodec<Skeleton>::Deserialize(
			core::file::read_file_bytes(
				(project.dataRoot / "Derived/Skeletons/unit.bskel").string()));
		const auto boxes = findPosedBounds(clips, mesh, rig);
		CHECK(boxes[0].has_value());

		const AnimationSet was =
			AssetCodec<AnimationSet>::Deserialize(before.at("Derived/Animations/unit.banim"));
		CHECK(clips.posedBoxes.size() == was.posedBoxes.size());
		CHECK(clips.clips.size() == was.clips.size());
		CHECK(clips.skeleton == was.skeleton);
		for (size_t i = 0; i < std::min(clips.posedBoxes.size(), was.posedBoxes.size()); ++i)
		{
			INFO("posed box " << i);
			CHECK(clips.posedBoxes[i].sourceSignature == was.posedBoxes[i].sourceSignature);
		}
		INFO("re-serialized payload");
		CHECK(
			AssetCodec<AnimationSet>::Serialize(clips) == AssetCodec<AnimationSet>::Serialize(was));
		INFO("source ref");
		CHECK(clips.source.key == was.source.key);
		CHECK(clips.source.stamp.size == was.source.stamp.size);
		CHECK(clips.source.stamp.hash == was.source.stamp.hash);
		CHECK(clips.source.parametersHash == was.source.parametersHash);
	}

	SECTION("and a second run writes nothing, since none of them is absent or stale any more")
	{
		const ReimportReport again = project.Store().Reimport(/*dryRun*/ false);
		CHECK(again.GetWrittenCount() == 0);
		CHECK(again.GetFailedCount() == 0);
	}
}

TEST_CASE("Reimport puts back only what is missing", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_one_gltf");
	const ImportedProject   project("bernini_reimport_one", source.PackGlb());

	const auto before = DerivedFiles(project.dataRoot);
	fs::remove(project.dataRoot / "Derived/Meshes/unit.bmesh");

	SECTION("a dry run reports it and writes nothing")
	{
		const ReimportReport report = project.Store().Reimport(/*dryRun*/ true);
		REQUIRE(report.sources.size() == 1);
		CHECK(report.sources[0].written == std::vector<std::string>{ "Derived/Meshes/unit.bmesh" });
		CHECK_FALSE(fs::exists(project.dataRoot / "Derived/Meshes/unit.bmesh"));
	}

	SECTION("a real run writes exactly it")
	{
		const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
		REQUIRE(report.sources.size() == 1);
		CHECK(report.sources[0].written == std::vector<std::string>{ "Derived/Meshes/unit.bmesh" });
		CheckSameFiles(DerivedFiles(project.dataRoot), before);
	}
}

TEST_CASE("An emptied texture folder is re-extracted", "[reimport]")
{
	// apples.glb, not the synthetic rig: it is the only fixture carrying real images, and an
	// extract with nothing to extract would pass this test without proving anything.
	const Project  project  = MakeTexProject();
	const fs::path dataRoot = project.GetDataDirectory();

	test::ImportUnitGroup(
		dataRoot,
		"assets/apples.glb",
		"Authored/Materials/red.bmaterial",
		30.0f,
		"Derived/BakedTextures/unit");

	const fs::path folder = dataRoot / "Derived/BakedTextures/unit";
	REQUIRE(fs::exists(folder));
	const auto before = DerivedFiles(dataRoot);

	// A fresh checkout of a project that gitignores its derived tree: the folder is not there, and
	// the source has not moved -- so the stamp the texture key compares still matches, and nothing
	// else in the library would notice.
	fs::remove_all(folder);
	REQUIRE(AssetStore(dataRoot).GetStaleImportedTextureSources().empty());

	const ReimportReport report = AssetStore(dataRoot).Reimport(/*dryRun*/ false);
	CHECK(report.GetFailedCount() == 0);
	CHECK(fs::exists(folder));
	CHECK_FALSE(fs::is_empty(folder));

	// The geometry is untouched by this: it was never absent.
	CheckSameFiles(DerivedFiles(dataRoot), before);
}

// The document is now the only record of what a source produced, which puts it in the reference
// graph: an `outputs` entry naming a key that no longer exists reads as *absent* to the producing
// side, so a rename the document did not follow would put the old file back under its old name --
// silently, on the next machine to run migrate, on exactly the gitignored-derived-tree project this
// work exists to enable.
TEST_CASE("A renamed output is followed, not reproduced under its old name", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_rename_gltf");
	const ImportedProject   project("bernini_reimport_rename", source.PackGlb());

	const AssetStore& store = project.Store();

	SECTION("a renamed mesh")
	{
		store.RenameAsset(planRename(
			AssetRefGraph::Scan(store),
			"Derived/Meshes/unit.bmesh",
			"Derived/Meshes/hero.bmesh"));

		const ImportDocument document =
			loadImportDocument(store.GetFiles(), "Authored/Meshes/unit.bimport");
		CHECK(
			std::ranges::find(document.outputs, "Derived/Meshes/hero.bmesh") !=
			document.outputs.end());
		CHECK(
			std::ranges::find(document.outputs, "Derived/Meshes/unit.bmesh") ==
			document.outputs.end());

		const ReimportReport report = store.Reimport(/*dryRun*/ false);
		CHECK(report.GetWrittenCount() == 0);
		CHECK_FALSE(fs::exists(project.dataRoot / "Derived/Meshes/unit.bmesh"));
	}

	SECTION("a renamed rig")
	{
		store.RenameAsset(planRename(
			AssetRefGraph::Scan(store),
			"Derived/Skeletons/unit.bskel",
			"Derived/Skeletons/hero.bskel"));

		const ImportDocument document =
			loadImportDocument(store.GetFiles(), "Authored/Meshes/unit.bimport");
		CHECK(document.skeleton == "Derived/Skeletons/hero.bskel");

		const ReimportReport report = store.Reimport(/*dryRun*/ false);
		CHECK(report.GetWrittenCount() == 0);
		CHECK_FALSE(fs::exists(project.dataRoot / "Derived/Skeletons/unit.bskel"));
	}
}

// `outputs` is a claim about what a source produced, not a need, so it must not turn every imported
// container into one the project refuses to delete -- and the claim has to go with the file, or the
// next migrate reads it as absent and puts it back.
TEST_CASE("Deleting a produced container is allowed, and drops the claim", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_del_gltf");
	const ImportedProject   project("bernini_reimport_del", source.PackGlb());

	const AssetStore& store = project.Store();

	const DeletionPlan plan = planDeletion(AssetRefGraph::Scan(store), "Derived/Meshes/unit.bmesh");
	CHECK(plan.Allowed());
	CHECK(plan.blockers.empty());
	REQUIRE(plan.producers == std::vector<std::string>{ "Authored/Meshes/unit.bimport" });

	REQUIRE(store.DeleteAsset(plan).status == DeletionStatus::kDeleted);
	CHECK_FALSE(fs::exists(project.dataRoot / "Derived/Meshes/unit.bmesh"));

	const ImportDocument document =
		loadImportDocument(store.GetFiles(), "Authored/Meshes/unit.bimport");
	CHECK(
		std::ranges::find(document.outputs, "Derived/Meshes/unit.bmesh") == document.outputs.end());

	// The point of dropping it: without that, this call would reproduce what was just deleted.
	const ReimportReport report = store.Reimport(/*dryRun*/ false);
	CHECK(report.GetWrittenCount() == 0);
	CHECK_FALSE(fs::exists(project.dataRoot / "Derived/Meshes/unit.bmesh"));
}

TEST_CASE("Deleting an import document leaves what it produced", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_deldoc_gltf");
	const ImportedProject   project("bernini_reimport_deldoc", source.PackGlb());

	const AssetStore& store = project.Store();

	// A produced-by claim is not a reference, so it cannot be the thing that frees a container into
	// the cascade either: deleting the document must not take the rig and mesh with it.
	const DeletionPlan plan =
		planCascadeDeletion(AssetRefGraph::Scan(store), "Authored/Meshes/unit.bimport");
	CHECK(std::ranges::find(plan.cascade, "Derived/Meshes/unit.bmesh") == plan.cascade.end());
	CHECK(std::ranges::find(plan.cascade, "Derived/Skeletons/unit.bskel") == plan.cascade.end());
}

TEST_CASE("What a failed source wrote before it threw is still reported", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_partial_gltf");
	const ImportedProject   project("bernini_reimport_partial", source.PackGlb());

	fs::remove(project.dataRoot / "Derived/Meshes/unit.bmesh");
	fs::remove(project.dataRoot / "Derived/Animations/unit.banim");

	// The mesh is written before the clips are, and a directory standing where the `.banim` goes
	// is the cheapest way to fail only the second of them.
	fs::create_directories(project.dataRoot / "Derived/Animations/unit.banim");

	const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
	REQUIRE(report.sources.size() == 1);
	CHECK_FALSE(report.sources[0].message.empty());

	// The mesh really is on disk, so a report claiming nothing was written would be wrong.
	CHECK(report.sources[0].written == std::vector<std::string>{ "Derived/Meshes/unit.bmesh" });
	CHECK(fs::exists(project.dataRoot / "Derived/Meshes/unit.bmesh"));
	CHECK(report.GetFailedCount() == 1);
}

TEST_CASE("A source that has gone is reported, not thrown", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_gone_gltf");
	const ImportedProject   project("bernini_reimport_gone", source.PackGlb());

	fs::remove(project.dataRoot / "Derived/Meshes/unit.bmesh");
	fs::remove(project.dataRoot / "Authored/Meshes/unit.glb");

	const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
	REQUIRE(report.sources.size() == 1);
	CHECK(report.sources[0].written.empty());
	CHECK_THAT(
		report.sources[0].message,
		Catch::Matchers::ContainsSubstring("is not in the project"));
}

TEST_CASE("Migrate produces what the sources name before it re-saves", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_migrate_gltf");
	const ImportedProject   project("bernini_reimport_migrate", source.PackGlb());

	const auto before = DerivedFiles(project.dataRoot);
	for (const auto& entry : before) fs::remove(project.dataRoot / entry.first);

	const MigrateReport report = project.Store().Migrate(/*dryRun*/ false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CheckSameFiles(DerivedFiles(project.dataRoot), before);
}

// The re-import reproduces what the writers produced, so it has to ground as they do -- and the
// case above already pins that byte-for-byte against a plain import. What it cannot see is the
// authored floor, which only reaches the writers through the document.
TEST_CASE("A re-import rests a clip on the floor its document authors", "[reimport][grounding]")
{
	const test::SkinnedGltf source("bernini_reimport_ground_gltf");
	const ImportedProject   project("bernini_reimport_ground", source.PackGlb());

	const fs::path     banim = project.dataRoot / "Derived/Animations/unit.banim";
	const AnimationSet cooked =
		AssetCodec<AnimationSet>::Deserialize(core::file::read_file_bytes(banim.string()));
	REQUIRE(cooked.clips.size() >= 2);

	const fs::path documentPath = project.dataRoot / "Authored/Meshes/unit.bimport";
	ImportDocument document     = loadImportDocument(documentPath);
	document.clipFloors         = { { std::string(cooked.stringPool.at(cooked.clips[0].nameOffset)),
		                              0.25f } };
	core::file::write_atomic(documentPath, AssetCodec<ImportDocument>::Serialize(document));

	fs::remove(banim);
	const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
	REQUIRE(report.GetFailedCount() == 0);

	const AnimationSet again =
		AssetCodec<AnimationSet>::Deserialize(core::file::read_file_bytes(banim.string()));
	REQUIRE(again.clips.size() == cooked.clips.size());

	// The authored floor, not the one the cook measured for itself.
	CHECK(again.clips[0].groundOffset == Catch::Approx(0.25f));
	CHECK(again.clips[0].groundOffset != Catch::Approx(cooked.clips[0].groundOffset));

	// Named clips only: the one the document says nothing about is still measured.
	CHECK(again.clips[1].groundOffset == Catch::Approx(cooked.clips[1].groundOffset));
}

// The two rules a threaded rebuild has to keep, and neither is visible in the report it returns:
// what it says it is doing while it does it, and that a stage is finished before the next begins.

TEST_CASE("A rebuild names every container it produces, exactly once", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_progress_gltf");
	const ImportedProject   project("bernini_reimport_progress", source.PackGlb());

	const auto before = DerivedFiles(project.dataRoot);
	REQUIRE(before.size() == 3);
	for (const auto& entry : before) fs::remove(project.dataRoot / entry.first);

	test::RecordedProgress recorded;
	const ReimportReport   report = project.Store().Reimport(/*dryRun*/ false, recorded.Sink());
	REQUIRE(report.GetFailedCount() == 0);

	// Every container the run wrote was announced before it was written, and nothing else was.
	auto announced = std::vector<std::string>();
	for (const test::RecordedStep& event : recorded.events)
	{
		CHECK(event.phase == ProgressPhase::kRegenerating);
		announced.emplace_back(event.subject);
	}
	std::ranges::sort(announced);

	auto wrote = std::vector<std::string>();
	for (const ReimportedSource& entry : report.sources)
		wrote.insert(wrote.end(), entry.written.begin(), entry.written.end());
	std::ranges::sort(wrote);

	CHECK(announced == wrote);

	// The count the sink was handed up front is the count it was then stepped through: a total
	// that drifts from the work is a bar that never fills or fills early.
	REQUIRE_FALSE(recorded.events.empty());
	const size_t total = recorded.events.front().total;
	CHECK(total == wrote.size());

	auto steps = std::vector<size_t>();
	for (const test::RecordedStep& event : recorded.events)
	{
		CHECK(event.total == total);
		steps.push_back(event.done);
	}
	std::ranges::sort(steps);
	CHECK(std::ranges::adjacent_find(steps) == steps.end());  // no step reported twice
	CHECK(steps.front() == 0);
	CHECK(steps.back() == total - 1);
}

TEST_CASE("Two sources rebuild together, but a stage finishes before the next starts", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_two_gltf");

	// Two independent groups, so a stage has something to fan out across.
	const fs::path root = fs::temp_directory_path() / "bernini_reimport_two";
	fs::remove_all(root);
	Project project = Project::Create(root / "Reimport.bproj", "Reimport");

	const fs::path glb      = source.PackGlb();
	const fs::path dataRoot = project.GetDataDirectory();
	test::ImportUnitGroup(dataRoot, glb, "Authored/Materials/red.bmaterial", 30.0f, {}, "one");
	test::ImportUnitGroup(dataRoot, glb, "Authored/Materials/red.bmaterial", 30.0f, {}, "two");
	project.ReloadStore();

	// Five, not six: the second group binds the rig the first wrote rather than forking it, so
	// `two.banim` addresses `one.bskel` -- which is exactly the cross-source dependency the stage
	// barrier exists for.
	const auto before = DerivedFiles(dataRoot);
	REQUIRE(before.size() == 5);
	REQUIRE(before.contains("Derived/Skeletons/one.bskel"));
	REQUIRE_FALSE(before.contains("Derived/Skeletons/two.bskel"));
	for (const auto& entry : before) fs::remove(dataRoot / entry.first);

	test::RecordedProgress recorded;
	const ReimportReport   report = project.GetStore().Reimport(/*dryRun*/ false, recorded.Sink());
	REQUIRE(report.GetFailedCount() == 0);
	CHECK(report.GetWrittenCount() == 5);

	// Threaded or not, what lands is what the import wrote.
	CheckSameFiles(DerivedFiles(dataRoot), before);

	// The barrier: a mesh names the rig it binds, and a clip set sweeps its boxes through the
	// meshes standing on disk. So no `.bmesh` may be announced before the last `.bskel`, and no
	// `.banim` before the last `.bmesh` -- which is exactly what a fully parallel run would break.
	REQUIRE(recorded.events.size() == 5);

	const std::span<const test::RecordedStep> steps = recorded.events;
	REQUIRE(test::RecordedProgress::CountOf(steps, ".bskel") == 1);
	REQUIRE(test::RecordedProgress::CountOf(steps, ".bmesh") == 2);
	REQUIRE(test::RecordedProgress::CountOf(steps, ".banim") == 2);

	CHECK(
		test::RecordedProgress::LastOf(steps, ".bskel") <
		test::RecordedProgress::FirstOf(steps, ".bmesh"));
	CHECK(
		test::RecordedProgress::LastOf(steps, ".bmesh") <
		test::RecordedProgress::FirstOf(steps, ".banim"));
}
