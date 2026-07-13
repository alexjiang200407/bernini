#include <QApplication>

#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <core/err/util.h>

#include "MainWindow.h"

namespace
{
	void
	LogToFile(QtMsgType type, const QMessageLogContext&, const QString& message)
	{
		const char* level = "unknown";
		switch (type)
		{
		case QtDebugMsg:
			level = "debug";
			break;
		case QtInfoMsg:
			level = "info";
			break;
		case QtWarningMsg:
			level = "warning";
			break;
		case QtCriticalMsg:
			level = "critical";
			break;
		case QtFatalMsg:
			level = "fatal";
			break;
		}

		QFile file(QCoreApplication::applicationDirPath() + "/editor.log");
		if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
		{
			QTextStream out(&file);
			out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " [" << level << "] "
				<< message << '\n';
		}
	}
}

int
main(int argc, char* argv[])
{
	core::install_crash_handlers();

	QApplication app(argc, argv);

	qInstallMessageHandler(LogToFile);

	MainWindow window;
	window.show();

	return app.exec();
}
