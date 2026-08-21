#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/pak_pack.h>
#include <assetlib_structs/BMaterial.h>

#include <nlohmann/json.hpp>

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
	CHECK(Project::IsRequiredDirectory(c_TexturesDirectoryName));
	CHECK(Project::IsRequiredDirectory(c_TexturesSrcDirectoryName));
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
		CHECK(Project::IsRequiredDirectory("Meshes/.."));
	}

	SECTION("but a folder made inside one is")
	{
		CHECK_FALSE(Project::IsRequiredDirectory("textures_src/kirk"));
		CHECK_FALSE(Project::IsRequiredDirectory("Materials/kirk"));

		// Only the categories themselves, at the top. A folder that merely shares the name is the user's.
		CHECK_FALSE(Project::IsRequiredDirectory("Meshes/Meshes"));
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
	material.pbr.baseColorTexture = "Textures/skin.ktx2";
	assetlib::saveMaterial(material, created.GetDataDirectory() / "Materials/skin.bmaterial");

	created.ReloadStore();

	CHECK(created.GetStore().GetDataRoot() == created.GetDataDirectory());
	CHECK(created.GetStore().Exists("Materials/skin.bmaterial"));

	// Everything the store answers for is writable, which is the property that lets the editor act
	// on anything it lists.
	CHECK_FALSE(created.GetStore().IsReadOnly());

	SECTION("an archive beside the project changes nothing about what it reads")
	{
		static_cast<void>(assetlib::packProject(
			assetlib::AssetStore(created.GetDataDirectory()),
			assetlib::PackDesc{ file.parent_path() / assetlib::c_DefaultArchiveName }));

		// Gone from the tree: the archive still holds it, and that is deliberately not consulted.
		fs::remove(created.GetDataDirectory() / "Materials/skin.bmaterial");

		Project reopened = Project::Open(file);

		CHECK_FALSE(reopened.GetStore().Exists("Materials/skin.bmaterial"));
	}
}
