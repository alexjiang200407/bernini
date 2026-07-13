#pragma once

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QString>
#include <catch2/catch_tostring.hpp>

namespace editor::test
{
	/** How long to wait for the UI to reach a state before giving up and letting the test fail. */
	inline constexpr int c_WaitMs = 10'000;

	/**
	 * Pumps the event loop until `ready` holds, and reports whether it ever did.
	 *
	 * This is what QTest spells QTRY_VERIFY. Catch2 has no notion of an event loop, and much of what
	 * Qt does happens off the calling thread -- a QFileSystemModel scanning a directory, a queued
	 * signal crossing back from a worker. None of it lands until the loop runs, so a test that simply
	 * asserts would be asserting against a state that has not arrived yet.
	 */
	template <typename Predicate>
	bool
	WaitFor(Predicate ready, int timeoutMs = c_WaitMs)
	{
		QDeadlineTimer deadline(timeoutMs);
		while (!ready() && !deadline.hasExpired())
			QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

		return ready();
	}
}

/** Lets a QString be streamed into an INFO(), a CAPTURE() or any other std::ostream. */
inline std::ostream&
operator<<(std::ostream& out, const QString& value)
{
	return out << value.toStdString();
}

namespace Catch
{
	// Without this Catch2 prints a QString as "{?}", which makes a failed comparison say nothing at
	// all about what was actually compared.
	template <>
	struct StringMaker<QString>
	{
		static std::string
		convert(const QString& value)
		{
			return '"' + value.toStdString() + '"';
		}
	};
}
