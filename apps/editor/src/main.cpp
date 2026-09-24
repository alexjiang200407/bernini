#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QProcess>
#include <QString>

#include <core/err/util.h>
#include <core/log/log.h>
#include <core/profiling/MemoryReport.h>

#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <qbytearrayview.h>
#include <qcontainerfwd.h>
#include <qcoreapplication.h>
#include <qlatin1stringview.h>
#include <qlogging.h>
#include <qobject.h>
#include <qstringlist.h>
#include <qtypes.h>
#include <spdlog/common.h>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "Plugins/plugin_loader.h"
#include "Startup/ProjectLauncher.h"
#include "Startup/StartupScreen.h"
#include "Startup/startup_project.h"
#include "util/editor_config.h"
#include "util/editor_language.h"
#include "util/qt_logging.h"
#include "util/recent_projects.h"

namespace
{
	// --mem-report <path>, read straight off argv rather than through QCommandLineParser: the
	// parser wants a QApplication that does not exist this early, and the report has to be armed
	// before anything allocates.
	std::filesystem::path
	MemoryReportPath(int argc, char* argv[])
	{
		for (int i = 1; i + 1 < argc; ++i)
		{
			if (std::string_view(argv[i]) == "--mem-report")
				return argv[i + 1];
		}
		return {};
	}

	// The project a restarted editor opens, outranking config.json's startupProject for that launch.
	constexpr auto c_ProjectArgument = QLatin1StringView("--project");

	// Read through QCoreApplication rather than argv, which on Windows is the ANSI code page and
	// would mangle a path outside it.
	std::filesystem::path
	ProjectArgument()
	{
		const QStringList arguments = QCoreApplication::arguments();
		const qsizetype   at        = arguments.indexOf(c_ProjectArgument);
		if (at < 0 || at + 1 >= arguments.size())
			return {};

		return std::filesystem::path(arguments[at + 1].toStdWString());
	}
}

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);
	QApplication::setStyle(new EditorStyle);

	const QString directory = QCoreApplication::applicationDirPath();

	// Before the window, so a diagnostic from the renderer's construction has somewhere to go: bgl
	// opens the log from its Graphics constructor, and everything before that would otherwise write
	// to a stdout a GUI launch does not have. bgl's own call then only applies its level.
	//
	// A log that will not open is inert rather than fatal -- there is nowhere to report a broken log
	// to, since this is what reporting is.
	try
	{
		core::logging::init_file_logger("editor.log", spdlog::level::info);
	}
	catch (const std::exception&)
	{}

	editor::InstallQtLogRouting();

	// After the log, so the report it writes on the way out has somewhere to go, and before the
	// window, so the peaks of building one are inside it.
	//
	// A named path is an explicit ask and outranks the config, which is the precedence every other
	// setting here follows.
	const std::filesystem::path configPath   = editor::DefaultConfigPath();
	const std::filesystem::path reportPath   = MemoryReportPath(argc, argv);
	auto                        memoryReport = std::optional<core::profiling::MemoryReport>();
	if (!reportPath.empty() || editor::MemoryReportEnabled(configPath))
		memoryReport.emplace(reportPath);

	const auto couldNotStart = [&directory](const std::exception& e) {
		qCritical("Editor: could not start: %s", e.what());

		QMessageBox::critical(
			nullptr,
			editor::Localize("editor.main.title", "Bernini Editor"),
			editor::Localize(
				"editor.main.could_not_start",
				{ e.what(), directory },
				"The editor could not start:\n\n{0}\n\nSee {1}/editor.log."));
	};

	// Before anything is shown: the launcher and the startup screen read the host's catalogs too.
	try
	{
		editor::InstallEditorLanguage(editor::ConfiguredLocale(configPath));
	}
	catch (const std::exception& e)
	{
		couldNotStart(e);
		return 1;
	}

	// Plugins first, because a project opens against their kinds: every descriptor under plugins/
	// beside the executable, then whatever config.json adds. The window registers their editor
	// halves once it builds.
	auto plugins = std::unique_ptr<editor::plugins::PluginSession>();
	try
	{
		std::vector<std::filesystem::path> directories =
			editor::plugins::DiscoverPluginDirectories(editor::plugins::DefaultPluginRoot());
		for (std::filesystem::path& directory :
		     editor::plugins::ConfiguredPluginDirectories(configPath))
			directories.push_back(std::move(directory));
		plugins =
			std::make_unique<editor::plugins::PluginSession>(editor::plugins::PluginSession::Load(
				directories,
				editor::plugins::CurrentBuildIdentity(),
				editor::plugins::DefaultPluginCopyRoot()));
	}
	catch (const std::exception& e)
	{
		couldNotStart(e);
		return 1;
	}

	// Opened before anything is built, because the project decides which surfaces the renderer
	// compiles: without one there is nothing to compile for, and the landing page asks instead.
	auto startupProject = editor::StartupProject();
	try
	{
		startupProject = editor::OpenStartupProject(ProjectArgument(), configPath, *plugins);
	}
	catch (const std::exception& e)
	{
		couldNotStart(e);
		return 1;
	}

	if (!startupProject.project)
	{
		// Nothing is on screen between the launcher closing on a choice and the startup screen
		// showing, which Qt may take for the last window closing and quit on.
		QApplication::setQuitOnLastWindowClosed(false);

		editor::ProjectLauncher launcher(
			editor::ReadRecentProjects(editor::RecentProjectsFileBeside(configPath)),
			*plugins,
			startupProject.failure);
		if (launcher.exec() != QDialog::Accepted)
			return 0;

		startupProject.project.emplace(launcher.TakeProject());
	}

	// Up before the window, because building the window is what takes the time: the renderer
	// compiles every pipeline it will ever use, which on a cold shader cache is tens of seconds
	// with nothing on screen at all. Hidden explicitly on both ways out below.
	editor::StartupScreen startup(editor::Localize("editor.main.title", "Bernini Editor"));
	startup.show();

	// Building the window creates the device, which fails on a machine rather than in the code -- a
	// driver that will not create a device, a budget too large to allocate. Reported rather than left
	// to terminate: a crash log is what a bug leaves, and none of these is one.
	auto window = std::optional<MainWindow>();
	try
	{
		// Everything between the splash and a usable editor, as one zone: the device and its
		// pipelines, the project's mount, its staleness scans and whatever they rebuild. What the
		// wall clock of a cold start is made of nests under this.
		ZoneScopedN("editor startup");

		window.emplace(
			std::move(plugins),
			std::move(*startupProject.project),
			configPath,
			startup.Sink());
	}
	catch (const std::exception& e)
	{
		startup.hide();
		couldNotStart(e);
		return 1;
	}

	// Hidden only once the window is up, so the desktop is never showing neither of them.
	window->show();
	startup.hide();
	QApplication::setQuitOnLastWindowClosed(true);

	const int status = app.exec();

	const std::filesystem::path relaunch = window->GetRelaunchProject();

	// Before the new process starts, so it does not create a device while this one still holds its own.
	window.reset();

	const QString relaunchPath = QString::fromStdWString(relaunch.wstring());
	if (!relaunch.empty() && !QProcess::startDetached(
								 QCoreApplication::applicationFilePath(),
								 { QString(c_ProjectArgument), relaunchPath }))
	{
		qCritical("Editor: could not restart into %s", qPrintable(relaunchPath));
	}

	return status;
}
