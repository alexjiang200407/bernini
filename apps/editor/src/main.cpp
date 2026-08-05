#include <QApplication>

#include <core/err/util.h>

#include "EditorStyle.h"
#include "MainWindow.h"
#include "util/FileLog.h"

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);
	QApplication::setStyle(new EditorStyle);

	editor::InstallFileLogger(QCoreApplication::applicationDirPath() + "/editor.log");

	MainWindow window;
	window.show();

	return app.exec();
}
