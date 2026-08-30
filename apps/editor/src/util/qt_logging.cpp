#include "util/qt_logging.h"

#include <QString>
#include <QtLogging>

#include <spdlog/spdlog.h>

namespace editor
{
	namespace
	{
		// Leaked, and assigned rather than reset: the handler stays installed until the process
		// ends, so a qWarning from a late destructor must never reach a logger static destruction
		// has already taken.
		std::shared_ptr<spdlog::logger>* g_Routed = nullptr;

		spdlog::level::level_enum
		LevelOf(const QtMsgType type) noexcept
		{
			switch (type)
			{
			case QtDebugMsg:
				return spdlog::level::debug;
			case QtInfoMsg:
				return spdlog::level::info;
			case QtWarningMsg:
				return spdlog::level::warn;
			case QtCriticalMsg:
				return spdlog::level::err;
			case QtFatalMsg:
				return spdlog::level::critical;
			}
			return spdlog::level::info;
		}
	}

	void
	InstallQtLogRouting()
	{
		const std::shared_ptr<spdlog::logger> current = spdlog::default_logger();

		auto routed = std::make_shared<spdlog::logger>(
			"qt",
			current->sinks().begin(),
			current->sinks().end());
		routed->set_level(spdlog::level::trace);

		g_Routed = new std::shared_ptr<spdlog::logger>(std::move(routed));

		qInstallMessageHandler(
			[](QtMsgType type, const QMessageLogContext&, const QString& message) {
				// Qt calls this through a C-style function pointer, so an exception leaving it would
				// unwind through Qt -- including from qFatal, and from threads Qt did not start.
				try
				{
					const std::shared_ptr<spdlog::logger>& log = *g_Routed;
					log->log(LevelOf(type), "{}", message.toStdString());

					// A crash is among the things worth logging, so nothing may be left sitting in
					// a buffer waiting for one -- and flush_on belongs to the renderer's level,
					// which is not this logger's.
					log->flush();
				}
				catch (...)
				{}
			});
	}
}
