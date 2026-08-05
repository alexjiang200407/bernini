#include <QApplication>
#include <QMessageBox>

#include <core/err/util.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>

#include "EditorConfig.h"
#include "EditorStyle.h"
#include "MainWindow.h"
#include "util/FileLog.h"

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);
	QApplication::setStyle(new EditorStyle);

	const QString directory = QCoreApplication::applicationDirPath();
	editor::InstallFileLogger(directory + "/editor.log");

	// Anything below can fail on a machine rather than in the code -- an unusable config, a driver
	// that will not create a device, a budget too large to allocate. Reported rather than left to
	// terminate: a crash log is what a bug leaves, and none of these is one.
	auto window = std::optional<MainWindow>();
	try
	{
		const core::Settings settings(
			core::file::get_executable_path().parent_path() / "config.json");

		window.emplace(EditorConfig::Parse(settings));
	}
	catch (const std::exception& e)
	{
		qCritical("Editor: could not start: %s", e.what());

		QMessageBox::critical(
			nullptr,
			QStringLiteral("Bernini Editor"),
			QStringLiteral("The editor could not start:\n\n%1\n\nSee %2/editor.log.")
				.arg(QString::fromUtf8(e.what()), directory));

		return 1;
	}

	window->show();
	return app.exec();
}
