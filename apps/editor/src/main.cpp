#include <QApplication>
#include <QMessageBox>

#include <core/err/util.h>
#include <core/log/log.h>
#include <core/profiling/MemoryReport.h>

#include <exception>
#include <filesystem>
#include <optional>
#include <qcoreapplication.h>
#include <qlogging.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <tracy/Tracy.hpp>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "Startup/StartupScreen.h"
#include "util/editor_config.h"
#include "util/qt_logging.h"

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
	const std::filesystem::path reportPath   = MemoryReportPath(argc, argv);
	auto                        memoryReport = std::optional<core::profiling::MemoryReport>();
	if (!reportPath.empty() || editor::MemoryReportEnabled(editor::DefaultConfigPath()))
		memoryReport.emplace(reportPath);

	// Up before the window, because building the window is what takes the time: the renderer
	// compiles every pipeline it will ever use, which on a cold shader cache is tens of seconds
	// with nothing on screen at all. Hidden explicitly on both ways out below.
	editor::StartupScreen startup(QStringLiteral("Bernini Editor"));
	startup.show();

	// Building the window reads config.json and creates the device, and both fail on a machine rather
	// than in the code -- an unusable config, a driver that will not create a device, a budget too
	// large to allocate. Reported rather than left to terminate: a crash log is what a bug leaves,
	// and none of these is one.
	auto window = std::optional<MainWindow>();
	try
	{
		// Everything between the splash and a usable editor, as one zone: the device and its
		// pipelines, the project's mount, its staleness scans and whatever they rebuild. What the
		// wall clock of a cold start is made of nests under this.
		ZoneScopedN("editor startup");

		window.emplace(nullptr, std::filesystem::path(), startup.Sink());
	}
	catch (const std::exception& e)
	{
		qCritical("Editor: could not start: %s", e.what());

		startup.hide();

		QMessageBox::critical(
			nullptr,
			QStringLiteral("Bernini Editor"),
			QStringLiteral("The editor could not start:\n\n%1\n\nSee %2/editor.log.")
				.arg(QString::fromUtf8(e.what()), directory));

		return 1;
	}

	// Hidden only once the window is up, so the desktop is never showing neither of them.
	window->show();
	startup.hide();

	return app.exec();
}
