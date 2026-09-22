#include "Startup/ProjectLauncher.h"
#include "Startup/startup_project.h"

#include "Plugins/plugin_loader.h"

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>
#include <QTemporaryDir>
#include <assetlib/Project.h>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

#include <filesystem>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <string>
#include <vector>

// main.cpp is outside editor_lib, so what it decides before the renderer exists is pinned here: the
// project the editor starts with, and what the landing page does when there is none.

namespace
{
	namespace fs = std::filesystem;

	struct Projects
	{
		QTemporaryDir temp;

		// main() loads plugins before it opens anything; these cases load none.
		editor::plugins::PluginSession plugins = editor::plugins::PluginSession::Load(
			{},
			editor::plugins::CurrentBuildIdentity(),
			fs::path(temp.path().toStdWString()) / "plugin-copies");

		[[nodiscard]] fs::path
		Root() const
		{
			return fs::path(temp.path().toStdWString());
		}

		[[nodiscard]] fs::path
		Create(const std::string& name) const
		{
			const fs::path file =
				Root() / name / (name + std::string(assetlib::Project::c_FileExtension));
			assetlib::Project::Create(file, name);
			return file;
		}

		// generic_string, because JSON has no raw backslash and a Windows path is full of them.
		[[nodiscard]] fs::path
		Config(const fs::path& startupProject) const
		{
			const fs::path config = Root() / "config.json";
			core::file::write_atomic(
				config,
				R"({ "startupProject": ")" + startupProject.generic_string() + R"(" })");
			return config;
		}
	};
}

TEST_CASE("The config's startup project is the one the editor opens", "[startup]")
{
	const Projects projects;
	const fs::path game = projects.Create("Game");

	const editor::StartupProject startup =
		editor::OpenStartupProject(fs::path(), projects.Config(game), projects.plugins);

	REQUIRE(startup.project.has_value());
	CHECK(startup.project->GetName() == "Game");
	CHECK(startup.failure.isEmpty());
}

// How a restart reaches the project the user opened, whatever config.json names.
TEST_CASE("A project named on the command line outranks the config's", "[startup]")
{
	const Projects projects;
	const fs::path game  = projects.Create("Game");
	const fs::path other = projects.Create("Other");

	const editor::StartupProject startup =
		editor::OpenStartupProject(other, projects.Config(game), projects.plugins);

	REQUIRE(startup.project.has_value());
	CHECK(startup.project->GetName() == "Other");
}

TEST_CASE("With no project named, the editor lands on the landing page", "[startup]")
{
	const Projects projects;

	const editor::StartupProject startup =
		editor::OpenStartupProject(fs::path(), projects.Config(fs::path()), projects.plugins);

	CHECK_FALSE(startup.project.has_value());

	// Nothing was asked for, so there is nothing to explain.
	CHECK(startup.failure.isEmpty());
}

// The empty state used to be where a bad path landed, and it cost a full renderer build to show a
// label. Now it is the landing page, which builds nothing, and says why.
TEST_CASE("A project that will not open lands on the landing page, saying why", "[startup]")
{
	const Projects projects;
	const fs::path gone = projects.Root() / "Gone" / "Gone.bproj";

	const editor::StartupProject startup =
		editor::OpenStartupProject(fs::path(), projects.Config(gone), projects.plugins);

	CHECK_FALSE(startup.project.has_value());
	CHECK(startup.failure.contains(QString::fromStdWString(gone.wstring())));
}

TEST_CASE("The landing page lists recent projects and opens the one chosen", "[startup]")
{
	const Projects projects;
	const fs::path game  = projects.Create("Game");
	const fs::path other = projects.Create("Other");

	editor::ProjectLauncher launcher({ other, game }, projects.plugins);

	auto* list = launcher.findChild<QListWidget*>("RecentProjects");
	REQUIRE(list != nullptr);
	REQUIRE(list->count() == 2);
	CHECK(list->item(0)->text().startsWith("Other"));
	CHECK(list->item(1)->text().startsWith("Game"));

	Q_EMIT list->itemActivated(list->item(1));

	REQUIRE(launcher.result() == QDialog::Accepted);
	CHECK(launcher.TakeProject().GetName() == "Game");
}

TEST_CASE("The landing page says why the startup project did not open", "[startup]")
{
	const Projects                projects;
	const editor::ProjectLauncher quiet({}, projects.plugins);
	CHECK(quiet.findChild<QLabel*>("LauncherNotice") == nullptr);

	const editor::ProjectLauncher told(
		{},
		projects.plugins,
		QStringLiteral("Could not open Gone.bproj"));
	const auto* notice = told.findChild<QLabel*>("LauncherNotice");
	REQUIRE(notice != nullptr);
	CHECK(notice->text() == QStringLiteral("Could not open Gone.bproj"));
}
