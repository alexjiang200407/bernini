#include "util/FileLog.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace
{
	// Every line the log writes: an ISO timestamp with milliseconds, a bracketed level, the message.
	// Anchored at both ends, so a line carrying part of another one does not match.
	const QRegularExpression c_LinePattern(
		QStringLiteral(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3} \[\w+\] [a-z]+-\d+$)"));

	QStringList
	ReadLines(const QString& path)
	{
		QFile file(path);
		REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));

		return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
	}
}

TEST_CASE("Concurrent writers never interleave a line", "[log]")
{
	// The bug this pins: the handler Qt installs is called on whatever thread logged -- the render
	// thread among them -- so two unsynchronized writers could split each other's lines in half.
	constexpr int c_Threads           = 4;
	constexpr int c_MessagesPerThread = 250;

	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const QString path = dir.filePath("editor.log");

	{
		editor::FileLog log(path);

		auto writers = std::vector<std::thread>();
		for (int t = 0; t < c_Threads; ++t)
		{
			writers.emplace_back([&log, t] {
				for (int i = 0; i < c_MessagesPerThread; ++i)
				{
					log.Write(QtWarningMsg, QStringLiteral("%1-%2").arg(QChar('a' + t)).arg(i));
				}
			});
		}

		for (std::thread& writer : writers) writer.join();
	}

	const QStringList lines = ReadLines(path);

	// Nothing lost and nothing torn: one line per message, each whole.
	REQUIRE(lines.size() == c_Threads * c_MessagesPerThread);
	for (const QString& line : lines) REQUIRE(c_LinePattern.match(line).hasMatch());
}

TEST_CASE("Every message reaches the file before Write returns", "[log]")
{
	// Held open rather than reopened per message, so nothing may be left in the stream's buffer: a
	// crash log is written by the crash that is about to lose it.
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const QString path = dir.filePath("editor.log");

	editor::FileLog log(path);
	log.Write(QtCriticalMsg, QStringLiteral("a-0"));

	const QStringList lines = ReadLines(path);
	REQUIRE(lines.size() == 1);
	REQUIRE(lines.front().contains("[critical]"));
}

TEST_CASE("A log that cannot be opened is inert rather than fatal", "[log]")
{
	// There is nowhere to report a broken log to -- this is what reporting is -- so Write does
	// nothing rather than throwing into whatever was being logged about.
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	// A directory is not a file, so opening it for append fails on every platform.
	editor::FileLog log(dir.path());

	REQUIRE_NOTHROW(log.Write(QtWarningMsg, QStringLiteral("a-0")));
}

TEST_CASE("Appending keeps what an earlier run wrote", "[log]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());

	const QString path = dir.filePath("editor.log");

	{
		editor::FileLog first(path);
		first.Write(QtInfoMsg, QStringLiteral("a-0"));
	}
	{
		editor::FileLog second(path);
		second.Write(QtInfoMsg, QStringLiteral("b-0"));
	}

	const QStringList lines = ReadLines(path);
	REQUIRE(lines.size() == 2);
	REQUIRE(lines.at(0).endsWith("a-0"));
	REQUIRE(lines.at(1).endsWith("b-0"));
}
