#include <QApplication>
#include <QMessageBox>

#include <core/err/util.h>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "util/FileLog.h"

namespace
{
	/** Puts `e` in front of the person at the window and in the log beside the binary. */
	void
	ReportFailure(const QString& directory, const char* stage, const std::exception& e)
	{
		qCritical("Editor: %s: %s", stage, e.what());

		QMessageBox::critical(
			nullptr,
			QStringLiteral("Bernini Editor"),
			QStringLiteral("The editor %1:\n\n%2\n\nSee %3/editor.log.")
				.arg(QString::fromUtf8(stage), QString::fromUtf8(e.what()), directory));
	}
}

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);
	QApplication::setStyle(new EditorStyle);

	const QString directory = QCoreApplication::applicationDirPath();
	editor::InstallFileLogger(directory + "/editor.log");

	// Building the window reads config.json and creates the device, and both fail on a machine rather
	// than in the code -- an unusable config, a driver that will not create a device, a budget too
	// large to allocate. Reported rather than left to terminate: a crash log is what a bug leaves,
	// and none of these is one.
	//
	// Both paths below exit 0 once the dialog has been shown: the failure has reached the person it
	// concerns, and a status the shell reports a second time only buries it.
	auto window = std::optional<MainWindow>();
	try
	{
		window.emplace();
	}
	catch (const std::exception& e)
	{
		ReportFailure(directory, "could not start", e);
		return 0;
	}

	window->show();

	try
	{
		return app.exec();
	}
	catch (const std::exception& e)
	{
		ReportFailure(directory, "stopped", e);
		return 0;
	}
}
