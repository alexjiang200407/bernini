#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>
#include <QtLogging>

namespace editor
{
	/**
	 * The editor's log file: one open handle, and one writer at a time.
	 *
	 * Qt calls an installed message handler on whatever thread logged, and the render thread logs --
	 * so Write is safe to call from several at once. Holding the file open matters for the same
	 * reason: the thing most likely to warn is a viewport already missing frames, and an open/close
	 * per message makes the stall it is reporting worse.
	 */
	class FileLog
	{
	public:
		/**
		 * Appends to `path`, creating it.
		 *
		 * A file that will not open leaves Write a no-op. Losing the log is not worth refusing to
		 * start over, and there is nowhere to report it to -- this is what reporting *is*.
		 */
		explicit FileLog(const QString& path);

		/**
		 * Serialized against every other Write, and safe from any thread.
		 *
		 * noexcept because this is the body of a Qt message handler: Qt calls it through a C-style
		 * function pointer, so an exception leaving it would unwind through Qt -- including from
		 * qFatal, and from threads Qt did not start.
		 */
		void
		Write(QtMsgType type, const QString& message) noexcept;

	private:
		QMutex      m_Mutex;
		QFile       m_File;
		QTextStream m_Stream;
	};

	/**
	 * Routes Qt's logging to `path` for the rest of the process.
	 *
	 * Call once. The log it opens outlives every object that might write to it on the way down, so a
	 * later call keeps the first file rather than swapping it.
	 */
	void
	InstallFileLogger(const QString& path);
}
