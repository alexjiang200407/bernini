#include <assetlib/codecs.h>
#include <assetlib/reimport.h>

#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <core/file/file.h>

#include "ImportUnitGroup.h"
#include "MountAt.h"
#include "SkinnedGltf.h"

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
			test::ImportUnitGroup(dataRoot, glb, "Materials/red.bmaterial", 30.0f);
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
			return Project::Create(root / "Reimport.berniniproject", "Reimport");
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

	/** Per key, so a failure names the container that drifted rather than "three maps differ". */
	void
	CheckSameFiles(
		const std::map<std::string, std::vector<std::byte>>& got,
		const std::map<std::string, std::vector<std::byte>>& want)
	{
		auto gotKeys  = std::vector<std::string>();
		auto wantKeys = std::vector<std::string>();
		for (const auto& [key, bytes] : got) gotKeys.push_back(key);
		for (const auto& [key, bytes] : want) wantKeys.push_back(key);
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
	REQUIRE(before.contains("Meshes/unit.bmesh"));
	REQUIRE(before.contains("Skeletons/unit.bskel"));
	REQUIRE(before.contains("Animations/unit.banim"));

	for (const auto& [key, bytes] : before) fs::remove(project.dataRoot / key);

	const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
	REQUIRE(report.FailedCount() == 0);
	CHECK(report.WrittenCount() == 3);

	CheckSameFiles(DerivedFiles(project.dataRoot), before);

	// The box a skinned load reads: if the production had baked none, a load would fall back to
	// measuring one, which is the cost the bake exists to remove.
	{
		const AnimationSet clips = AssetCodec<AnimationSet>::Deserialize(
			core::file::read_file_bytes((project.dataRoot / "Animations/unit.banim").string()));
		const BMesh mesh = AssetCodec<BMesh>::Deserialize(
			core::file::read_file_bytes((project.dataRoot / "Meshes/unit.bmesh").string()));
		const Skeleton rig = AssetCodec<Skeleton>::Deserialize(
			core::file::read_file_bytes((project.dataRoot / "Skeletons/unit.bskel").string()));
		const auto boxes = findPosedBounds(clips, mesh, rig);
		CHECK(boxes[0].has_value());

		const AnimationSet was =
			AssetCodec<AnimationSet>::Deserialize(before.at("Animations/unit.banim"));
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
		CHECK(again.WrittenCount() == 0);
		CHECK(again.FailedCount() == 0);
	}
}

TEST_CASE("Reimport puts back only what is missing", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_one_gltf");
	const ImportedProject   project("bernini_reimport_one", source.PackGlb());

	const auto before = DerivedFiles(project.dataRoot);
	fs::remove(project.dataRoot / "Meshes/unit.bmesh");

	SECTION("a dry run reports it and writes nothing")
	{
		const ReimportReport report = project.Store().Reimport(/*dryRun*/ true);
		REQUIRE(report.sources.size() == 1);
		CHECK(report.sources[0].written == std::vector<std::string>{ "Meshes/unit.bmesh" });
		CHECK_FALSE(fs::exists(project.dataRoot / "Meshes/unit.bmesh"));
	}

	SECTION("a real run writes exactly it")
	{
		const ReimportReport report = project.Store().Reimport(/*dryRun*/ false);
		REQUIRE(report.sources.size() == 1);
		CHECK(report.sources[0].written == std::vector<std::string>{ "Meshes/unit.bmesh" });
		CheckSameFiles(DerivedFiles(project.dataRoot), before);
	}
}

TEST_CASE("A source that has gone is reported, not thrown", "[reimport]")
{
	const test::SkinnedGltf source("bernini_reimport_gone_gltf");
	const ImportedProject   project("bernini_reimport_gone", source.PackGlb());

	fs::remove(project.dataRoot / "Meshes/unit.bmesh");
	fs::remove(project.dataRoot / "meshes_src/unit.glb");

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
	for (const auto& [key, bytes] : before) fs::remove(project.dataRoot / key);

	const MigrateReport report = project.Store().Migrate(/*dryRun*/ false);
	CHECK(report.Count(MigratedFile::Outcome::kFailed) == 0);
	CheckSameFiles(DerivedFiles(project.dataRoot), before);
}
