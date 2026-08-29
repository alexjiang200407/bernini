#include <QApplication>
#include <QMessageBox>

#include <core/err/util.h>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "Startup/StartupScreen.h"
#include "util/FileLog.h"

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);
	QApplication::setStyle(new EditorStyle);

	const QString directory = QCoreApplication::applicationDirPath();
	editor::InstallFileLogger(directory + "/editor.log");

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
