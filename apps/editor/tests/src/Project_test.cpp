#include "Project/Project.h"

#include "util/QtSupport.h"

#include <QTemporaryDir>
#include <nlohmann/json.hpp>

namespace
{
	namespace fs = std::filesystem;

	/** Every directory Project promises to scaffold under Data/. */
	const std::array<std::string_view, 5> c_DataDirectories = {
		Project::c_MeshesDirectoryName,      Project::c_TexturesDirectoryName,
		Project::c_TexturesSrcDirectoryName, Project::c_MaterialsDirectoryName,
		Project::c_LevelsDirectoryName,
	};

	/** An empty directory, and the path of a project file that does not exist inside it yet. */
	struct Sandbox
	{
		QTemporaryDir temp;

		fs::path
		ProjectFile() const
		{
			return temp.path().toStdString() / fs::path("MyGame") /
			       ("MyGame" + std::string(Project::c_FileExtension));
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
	const Sandbox sandbox;

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	for (const std::string_view directory : c_DataDirectories)
	{
		INFO("Data/" << directory);
		REQUIRE(fs::is_directory(project.GetDataDirectory() / directory));
	}
}

TEST_CASE("Creating a project brings its root directory into being", "[project]")
{
	const Sandbox sandbox;

	// The MyGame/ directory does not exist yet -- Create is what makes it, which is exactly what the
	// editor's "new project" flow relies on.
	REQUIRE(!fs::exists(sandbox.ProjectFile().parent_path()));

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	REQUIRE(fs::is_directory(sandbox.ProjectFile().parent_path()));
	REQUIRE(fs::is_regular_file(project.GetProjectFile()));
}

TEST_CASE("Creating a project writes its metadata", "[project]")
{
	const Sandbox sandbox;

	Project::Create(sandbox.ProjectFile(), "MyGame");

	const nlohmann::json json = nlohmann::json::parse(ReadText(sandbox.ProjectFile()));

	REQUIRE(json.value("name", std::string()) == "MyGame");
	REQUIRE(json.value("version", 0) == 1);
	REQUIRE(json.value("dataDirectory", std::string()) == "Data");
}

TEST_CASE("A project keeps the name it was given", "[project]")
{
	const Sandbox sandbox;

	// The display name is not the file name, and must not be quietly derived from it.
	const Project project = Project::Create(sandbox.ProjectFile(), "Something Else Entirely");

	REQUIRE(project.GetName() == "Something Else Entirely");
}

TEST_CASE("The data directory hangs off the project file", "[project]")
{
	const Sandbox sandbox;

	const Project project = Project::Create(sandbox.ProjectFile(), "MyGame");

	REQUIRE(project.GetDataDirectory() == sandbox.ProjectFile().parent_path() / "Data");
}

TEST_CASE("Opening a project round-trips what creating it wrote", "[project]")
{
	const Sandbox sandbox;

	const Project created = Project::Create(sandbox.ProjectFile(), "Round Trip");
	const Project opened  = Project::Open(sandbox.ProjectFile());

	REQUIRE(opened.GetName() == created.GetName());
	REQUIRE(opened.GetProjectFile() == created.GetProjectFile());
	REQUIRE(opened.GetDataDirectory() == created.GetDataDirectory());
}

TEST_CASE("An unnamed project falls back to its file name", "[project]")
{
	const Sandbox sandbox;

	WriteText(sandbox.ProjectFile(), R"({ "version": 1 })");

	REQUIRE(Project::Open(sandbox.ProjectFile()).GetName() == "MyGame");
}

TEST_CASE("An unversioned project is read as current", "[project]")
{
	const Sandbox sandbox;

	// Nothing observable hangs off the version yet. This pins the behaviour before something does.
	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame" })");

	Project::Open(sandbox.ProjectFile()).Save();

	REQUIRE(nlohmann::json::parse(ReadText(sandbox.ProjectFile())).value("version", 0) == 1);
}

TEST_CASE("An older project survives a round trip as an older project", "[project]")
{
	const Sandbox sandbox;

	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame", "version": 0 })");

	// Open does not migrate, and Save writes back what it read -- so an old file is not silently
	// stamped as current.
	Project::Open(sandbox.ProjectFile()).Save();

	REQUIRE(nlohmann::json::parse(ReadText(sandbox.ProjectFile())).value("version", -1) == 0);
}

TEST_CASE("Opening a project recreates a missing data directory", "[project]")
{
	const Sandbox sandbox;

	Project::Create(sandbox.ProjectFile(), "MyGame");

	const fs::path meshes =
		sandbox.ProjectFile().parent_path() / "Data" / Project::c_MeshesDirectoryName;
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
	const Sandbox sandbox;

	const Project created = Project::Create(sandbox.ProjectFile(), "MyGame");

	const fs::path asset = created.GetDataDirectory() / Project::c_MeshesDirectoryName / "a.bmesh";
	WriteText(asset, "not really a mesh");

	Project::Open(sandbox.ProjectFile());

	// Healing must not mean recreating, or opening a project would empty it.
	REQUIRE(fs::is_regular_file(asset));
	REQUIRE(ReadText(asset) == "not really a mesh");
}

TEST_CASE("A project that is not there cannot be opened", "[project]")
{
	const Sandbox sandbox;

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("A malformed project cannot be opened", "[project]")
{
	const Sandbox sandbox;

	WriteText(sandbox.ProjectFile(), "{ this is not json");

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("A data directory blocked by a file is refused", "[project]")
{
	const Sandbox sandbox;

	WriteText(sandbox.ProjectFile(), R"({ "name": "MyGame", "version": 1 })");

	// Something already occupies Data/Meshes, and it is not a directory. Open cannot scaffold over
	// it, and has to say so rather than carry on with a project that has nowhere to put a mesh.
	WriteText(
		sandbox.ProjectFile().parent_path() / "Data" / Project::c_MeshesDirectoryName,
		"in the way");

	REQUIRE_THROWS_AS(Project::Open(sandbox.ProjectFile()), std::runtime_error);
}

TEST_CASE("Saving a project twice writes the same project", "[project]")
{
	const Sandbox sandbox;

	const Project     project     = Project::Create(sandbox.ProjectFile(), "MyGame");
	const std::string afterCreate = ReadText(sandbox.ProjectFile());

	project.Save();

	REQUIRE(ReadText(sandbox.ProjectFile()) == afterCreate);
}
