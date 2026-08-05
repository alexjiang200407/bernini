#include "util/FileLog.h"

#include <QDateTime>

namespace editor
{
	namespace
	{
		const char*
		LevelName(QtMsgType type) noexcept
		{
			switch (type)
			{
			case QtDebugMsg:
				return "debug";
			case QtInfoMsg:
				return "info";
			case QtWarningMsg:
				return "warning";
			case QtCriticalMsg:
				return "critical";
			case QtFatalMsg:
				return "fatal";
			}
			return "unknown";
		}
	}

	FileLog::FileLog(const QString& path) : m_File(path)
	{
		if (m_File.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
			m_Stream.setDevice(&m_File);
	}

	void
	FileLog::Write(QtMsgType type, const QString& message) noexcept
	{
		const QMutexLocker locker(&m_Mutex);

		if (m_Stream.device() == nullptr)
			return;

		m_Stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " ["
				 << LevelName(type) << "] " << message << '\n';

		// A crash is among the things worth logging, so nothing may be left sitting in the buffer
		// waiting for one.
		m_Stream.flush();
	}

	void
	InstallFileLogger(const QString& path)
	{
		// Never destroyed: the handler stays installed until the process ends, and static destruction
		// order would otherwise let a late qWarning write to a closed file.
		static auto* const c_Log = new FileLog(path);

		qInstallMessageHandler([](QtMsgType type,
		                          const QMessageLogContext&,
		                          const QString& message) { c_Log->Write(type, message); });
	}
}
