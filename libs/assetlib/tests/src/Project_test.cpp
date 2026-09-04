#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/pak.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "RefsSandbox.h"
#include <assetlib/project_layout.h>

#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json_fwd.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	/** Every directory Project promises to scaffold under Data/, read from Project itself. */
	constexpr auto& c_DataDirectories = c_RequiredDirectories;

	/** An empty directory, and the path of a project file that does not exist inside it yet. */
	struct Sandbox
	{
		fs::path root;

		// Named per case, like assetlib::test::DataRoot: run_tests.py shards a suite across
		// concurrent processes, so two cases sharing a root would race each other's remove_all.
		explicit Sandbox(const char* name) : root(fs::temp_directory_path() / name)
		{
			fs::remove_all(root);
			fs::create_directories(root);
		}

		~Sandbox() { fs::remove_all(root); }

		fs::path
		ProjectFile() const
		{
			return root / "MyGame" / ("MyGame" + std::string(Project::c_FileExtension));
		}
	};

	void
	WriteText(const fs::path& path, std::string_view text)
	{
		fs::create_directories(path.parent_path());
		std::ofstream stream(path);
		stream << text;
	}

	std::string
	ReadText(const fs::path& path)
	{
		std::ifstream     stream(path);
		std::stringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}
}

TEST_CASE("Creating a project scaffolds the data tree", "[project]")
{
	const Sandbox sandbox("bernini_project_creating_project_scaffolds_data_tree");

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	for (const std::string_view directory : c_DataDirectories)
	{
		INFO("Data/" << directory);
		REQUIRE(fs::is_directory(project.GetDataDirectory() / directory));
	}
}

TEST_CASE("Creating a project brings its root directory into being", "[project]")
{
	const Sandbox sandbox("bernini_project_creating_project_brings_root_directory_into");

	// The MyGame/ directory does not exist yet -- Create is what makes it, which is exactly what the
	// editor's "new project" flow relies on.
	REQUIRE(!fs::exists(sandbox.ProjectFile().parent_path()));

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	REQUIRE(fs::is_directory(sandbox.ProjectFile().parent_path()));
	REQUIRE(fs::is_regular_file(project.GetProjectFile()));
}

TEST_CASE("Creating a project writes its metadata", "[project]")
{
	const Sandbox sandbox("bernini_project_creating_project_writes_metadata");

	Project::Create(sandbox.ProjectFile(), "MyGame");

	const nlohmann::json json = nlohmann::json::parse(ReadText(sandbox.ProjectFile()));

	REQUIRE(json.value("name", std::string()) == "MyGame");
	REQUIRE(json.value("version", 0) == 1);
	REQUIRE(json.value("dataDirectory", std::string()) == "Data");
}

TEST_CASE("A project keeps the name it was given", "[project]")
{
	const Sandbox sandbox("bernini_project_project_keeps_name_given");

	// The display name is not the file name, and must not be quietly derived from it.
	const Project project = Project::Create(sandbox.ProjectFile(), "Something Else Entirely");

	REQUIRE(project.GetName() == "Something Else Entirely");
}

TEST_CASE("The data directory hangs off the project file", "[project]")
{
	const Sandbox sandbox("bernini_project_data_directory_hangs_off_project_file");

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	REQUIRE(project.GetDataDirectory() == sandbox.ProjectFile().parent_path() / "Data");
}

TEST_CASE("Opening a project round-trips what creating it wrote", "[project]")
{
	const Sandbox sandbox("bernini_project_opening_project_roundtrips_creating_wrote");

	const Project created = Project::Create(sandbox.ProjectFile(), "Round Trip");
	const Project opened  = Project::Open(sandbox.ProjectFile());

	REQUIRE(opened.GetName() == created.GetName());
	REQUIRE(opened.GetProjectFile() == created.GetProjectFile());
	REQUIRE(opened.GetDataDirectory() == created.GetDataDirectory());
}

TEST_CASE("An unnamed project falls back to its file name", "[project]")
{
	const Sandbox sandbox("bernini_project_unnamed_project_falls_back_file_name");

	WriteText(sandbox.ProjectFile(), R"({ "version": 1 })");

	REQUIRE(Project::Open(sandbox.ProjectFile()).GetName() == "MyGame");
}

TEST_CASE("An unversioned project is read as current", "[project]")
{
	const Sandbox sandbox("bernini_project_unversioned_project_read_as_current");

	// Nothing observable hangs off the version yet. This pins the behaviour before something does.
	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame" })");

	Project::Open(sandbox.ProjectFile()).Save();

	REQUIRE(nlohmann::json::parse(ReadText(sandbox.ProjectFile())).value("version", 0) == 1);
}

TEST_CASE("An older project survives a round trip as an older project", "[project]")
{
	const Sandbox sandbox("bernini_project_older_project_survives_round_trip_as_older_p");

	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame", "version": 0 })");

	// Open does not migrate, and Save writes back what it read -- so an old file is not silently
	// stamped as current.
	Project::Open(sandbox.ProjectFile()).Save();

	REQUIRE(nlohmann::json::parse(ReadText(sandbox.ProjectFile())).value("version", -1) == 0);
}

TEST_CASE("Opening a project recreates a missing data directory", "[project]")
{
	const Sandbox sandbox("bernini_project_opening_project_recreates_missing_data_direc");

	Project::Create(sandbox.ProjectFile(), "MyGame");

	const fs::path meshes = sandbox.ProjectFile().parent_path() / "Data" / c_MeshesDirectoryName;
	fs::remove_all(meshes);
	REQUIRE(!fs::exists(meshes));

	// Open heals: a project whose Meshes/ was deleted, or that predates the directory existing at
	// all, still opens, with the tree put back.
	const Project project = Project::Open(sandbox.ProjectFile());

	REQUIRE(fs::is_directory(meshes));
	for (const std::string_view directory : c_DataDirectories)
		REQUIRE(fs::is_directory(project.GetDataDirectory() / directory));
}

TEST_CASE("Opening a project leaves what is already in it alone", "[project]")
{
	const Sandbox sandbox("bernini_project_opening_project_leaves_already_alone");

	const Project created = Project::Create(sandbox.ProjectFile(), "MyGame");

	const fs::path asset = created.GetDataDirectory() / c_MeshesDirectoryName / "a.bmesh";
	WriteText(asset, "not really a mesh");

	Project::Open(sandbox.ProjectFile());

	// Healing must not mean recreating, or opening a project would empty it.
	REQUIRE(fs::is_regular_file(asset));
	REQUIRE(ReadText(asset) == "not really a mesh");
}

TEST_CASE("A project that is not there cannot be opened", "[project]")
{
	const Sandbox sandbox("bernini_project_project_there_cannot_be_opened");

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("A malformed project cannot be opened", "[project]")
{
	const Sandbox sandbox("bernini_project_malformed_project_cannot_be_opened");

	WriteText(sandbox.ProjectFile(), "{ this is not json");

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("A data directory blocked by a file is refused", "[project]")
{
	const Sandbox sandbox("bernini_project_data_directory_blocked_by_file_refused");

	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame", "version": 1 })");

	// Something already occupies Data/Meshes, and it is not a directory. Open cannot scaffold over
	// it, and has to say so rather than carry on with a project that has nowhere to put a mesh.
	WriteText(sandbox.ProjectFile().parent_path() / "Data" / c_MeshesDirectoryName, "in the way");

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("Saving a project twice writes the same project", "[project]")
{
	const Sandbox sandbox("bernini_project_saving_project_twice_writes_same_project");

	const Project     project     = Project::Create(sandbox.ProjectFile(), "MyGame");
	const std::string afterCreate = ReadText(sandbox.ProjectFile());

	project.Save();

	REQUIRE(ReadText(sandbox.ProjectFile()) == afterCreate);
}

TEST_CASE("The scaffolded categories are not the user's to delete", "[project]")
{
	// Every asset path in the project is written against this layout, and Open puts a missing category
	// straight back -- so deleting one would not even stick.
	CHECK(Project::IsRequiredDirectory(c_MeshesDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_BakedTexturesDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_SourceTexturesDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_MaterialsDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_LevelsDirectoryName));

	// One per environment container: a `.benv` names a `.bsky` and a `.benvl`, and each lives in its
	// own category so the reference is a path the project layout guarantees.
	CHECK(Project::IsRequiredDirectory(c_EnvironmentsDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_EnvLightingDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_SkyDirectoryName));

	SECTION("nor is the data root they sit in, however it is spelled")
	{
		CHECK(Project::IsRequiredDirectory(""));
		CHECK(Project::IsRequiredDirectory("."));
		CHECK(Project::IsRequiredDirectory("Derived/Meshes/../.."));
	}

	SECTION("nor is either half, which holds every category under it")
	{
		CHECK(Project::IsRequiredDirectory(c_AuthoredDirectoryName));
		CHECK(Project::IsRequiredDirectory(c_DerivedDirectoryName));

		// `a/b/..` normalizes with a trailing slash, which is not how the halves are spelled.
		CHECK(Project::IsRequiredDirectory("Derived/Meshes/.."));
	}

	SECTION("but a folder made inside one is")
	{
		CHECK_FALSE(Project::IsRequiredDirectory("Derived/SourceTextures/kirk"));
		CHECK_FALSE(Project::IsRequiredDirectory("Authored/Materials/kirk"));

		// Only the categories themselves, at the top. A folder that merely shares the name is the user's.
		CHECK_FALSE(Project::IsRequiredDirectory("Derived/Meshes/Meshes"));
		CHECK_FALSE(Project::IsRequiredDirectory("Props"));
	}
}

/**
 * The editor's store is the loose tree and only ever the loose tree.
 *
 * An archive is what `pack` makes from this tree to ship, and what a shipped game mounts. Reading
 * one back here would list assets the editor cannot write, and would make the version-tracked unit
 * a single packed blob rather than the separate files it wants to be.
 */
TEST_CASE("A project reads and writes the loose tree, archive or not", "[project]")
{
	const Sandbox sandbox("bernini_project_project_reads_writes_loose_tree_archive_or");
	const auto    file = sandbox.ProjectFile();

	Project created = Project::Create(file, "MyGame");

	auto material                 = assetlib::BMaterial();
	material.name                 = "skin";
	material.pbr.baseColorTexture = "Derived/BakedTextures/skin.ktx2";
	created.GetStore().Save(material, "Authored/Materials/skin.bmaterial");

	created.ReloadStore();

	CHECK(created.GetStore().GetDataRoot() == created.GetDataDirectory());
	CHECK(created.GetStore().Exists("Authored/Materials/skin.bmaterial"));

	// Everything the store answers for is writable, which is the property that lets the editor act
	// on anything it lists.
	CHECK_FALSE(created.GetStore().IsReadOnly());

	SECTION("an archive beside the project changes nothing about what it reads")
	{
		static_cast<void>(
			assetlib::AssetStore(created.GetDataDirectory())
				.Pack(assetlib::PackDesc{ file.parent_path() / assetlib::c_DefaultArchiveName }));

		// Gone from the tree: the archive still holds it, and that is deliberately not consulted.
		fs::remove(created.GetDataDirectory() / "Authored/Materials/skin.bmaterial");

		Project reopened = Project::Open(file);

		CHECK_FALSE(reopened.GetStore().Exists("Authored/Materials/skin.bmaterial"));
	}
}

// The layout is an invariant rather than a convention: the codec decides a container's origin,
// and Save refuses a key naming the half the other origin owns. Without this the split holds only for
// as long as every caller remembers it, which is what let the environment importer write a `.bsky`
// outside `Derived/` for a whole release.
TEST_CASE(
	"A container cannot be written outside the half its codec belongs to",
	"[project][origin]")
{
	const test::DataRoot root("bernini_origin_rule");
	const AssetStore     store(root.path);

	SECTION("a derived container is refused under the authored half")
	{
		CHECK_THROWS_WITH(
			store.Save(BMesh(), "Authored/Meshes/a.bmesh"),
			Catch::Matchers::ContainsSubstring("belongs under Derived/"));
	}

	SECTION("an authored document is refused under the derived half")
	{
		CHECK_THROWS_WITH(
			store.Save(BMaterial(), "Derived/Materials/a.bmaterial"),
			Catch::Matchers::ContainsSubstring("belongs under Authored/"));
	}

	SECTION("a key in neither half is refused, not silently treated as one of them")
	{
		CHECK_THROWS_AS(store.Save(BMesh(), "a.bmesh"), std::runtime_error);
		CHECK_THROWS_AS(store.Save(BMaterial(), "Materials/a.bmaterial"), std::runtime_error);
	}

	SECTION("the origin is read through a normalized key, so a detour cannot smuggle one past")
	{
		CHECK_THROWS_AS(
			store.Save(BMesh(), "Derived/../Authored/Meshes/a.bmesh"),
			std::runtime_error);
		CHECK_NOTHROW(store.Save(BMesh(), "Authored/../Derived/Meshes/a.bmesh"));
	}

	SECTION("the message names the container, as every other message this library throws does")
	{
		CHECK_THROWS_WITH(
			store.Save(BSky(), "Authored/Sky/a.bsky"),
			Catch::Matchers::StartsWith("bsky: "));
	}
}

// originOf is the predicate the rule is built on, and the cases that decide nothing are the ones a
// caller is most likely to get wrong.
TEST_CASE("originOf answers only for a key inside a half", "[project][origin]")
{
	CHECK(originOf("Authored/Materials/a.bmaterial") == AssetOrigin::kAuthored);
	CHECK(originOf("Derived/Meshes/a.bmesh") == AssetOrigin::kDerived);

	// A half names itself, not something in it.
	CHECK_FALSE(originOf("Authored").has_value());
	CHECK_FALSE(originOf("Derived").has_value());

	CHECK_FALSE(originOf("").has_value());
	CHECK_FALSE(originOf("a.bmesh").has_value());
	CHECK_FALSE(originOf("Meshes/a.bmesh").has_value());

	// Not a prefix match: `DerivedThings` is not `Derived`.
	CHECK_FALSE(originOf("DerivedThings/a.bmesh").has_value());
}
