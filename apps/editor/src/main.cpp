#include <QApplication>
#include <QMessageBox>

#include <core/err/util.h>
#include <core/log/log.h>

#include <spdlog/spdlog.h>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "Startup/StartupScreen.h"
#include "util/qt_logging.h"

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
