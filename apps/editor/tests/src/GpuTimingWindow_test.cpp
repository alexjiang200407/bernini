#include "Windows/GpuTiming/GpuTimingWindow.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <bgl/PassTiming.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <qbuffer.h>
#include <qcontainerfwd.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qwidget.h>
#include <vector>

// The window itself, which needs no graphics device: what it records, what it forgets, and what it
// writes out. The picture is pinned next door in pass_graph_paint_test.

namespace
{
	[[nodiscard]] std::vector<bgl::PassTimings>
	Frames(const uint64_t first, const uint64_t last)
	{
		std::vector<bgl::PassTimings> frames;
		for (uint64_t frame = first; frame <= last; ++frame)
		{
			frames.push_back(
				bgl::PassTimings{ .frame  = frame,
			                      .passes = { { .name = "Clear", .milliseconds = 0.1 },
			                                  { .name = "Forward 0", .milliseconds = 3.0 } } });
		}
		return frames;
	}
}

TEST_CASE("The window records the frames it is given", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Level Editor");

	window.AddFrames(Frames(1, 5));

	CHECK(window.History().SampleCount() == 5);
}

TEST_CASE("A different viewport forgets the frames of the one before", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Level Editor");
	window.AddFrames(Frames(1, 5));

	window.SetSource("Material Editor");

	// One viewport's frame cost says nothing about another's, and the two would be plotted as one
	// series if the history carried over.
	CHECK(window.History().SampleCount() == 0);
}

TEST_CASE("The same viewport reported again keeps the history", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Level Editor");
	window.AddFrames(Frames(1, 5));

	window.SetSource("Level Editor");

	CHECK(window.History().SampleCount() == 5);
}

TEST_CASE("A window on screen asks for timing, and stops asking when it closes", "[gputiming]")
{
	editor::GpuTimingWindow window;
	QSignalSpy              wanted(&window, &editor::GpuTimingWindow::TimingWanted);

	window.show();
	REQUIRE(wanted.count() == 1);
	CHECK(wanted.at(0).at(0).toBool());

	window.close();
	REQUIRE(wanted.count() == 2);
	CHECK_FALSE(wanted.at(1).at(0).toBool());
}

TEST_CASE("An export writes every frame the graph holds, and nothing else", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Level Editor");
	window.AddFrames(Frames(1, 30));

	const QTemporaryDir directory;
	REQUIRE(directory.isValid());

	const QString file = window.Export(QDir(directory.path()));
	REQUIRE(!file.isEmpty());

	const QDir written(directory.path());
	REQUIRE(QFile::exists(written.filePath(file)));

	// Nothing else: the picture is on screen, and a drawing of it in the folder would be a second
	// thing to keep in step for a reader who cannot ask it anything.
	CHECK(written.entryList(QDir::Files).size() == 1);

	// The CSV is the history, not a summary of it: one row per frame recorded, under a header.
	QFile csv(written.filePath(file));
	REQUIRE(csv.open(QIODevice::ReadOnly | QIODevice::Text));
	CHECK(QString::fromUtf8(csv.readAll()).split('\n', Qt::SkipEmptyParts).size() == 31);
}

TEST_CASE("An export with nothing recorded writes no files", "[gputiming]")
{
	editor::GpuTimingWindow window;

	const QTemporaryDir directory;
	REQUIRE(directory.isValid());

	CHECK(window.Export(QDir(directory.path())).isEmpty());
	CHECK(QDir(directory.path()).entryList(QDir::Files).isEmpty());
}

TEST_CASE("Paused, the window records nothing until it resumes", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Level Editor");
	window.AddFrames(Frames(1, 3));

	// The button is what a person reaches for, and pausing is the only way a spike stays on screen
	// long enough to be read.
	auto* pause = window.findChild<QWidget*>("GpuTimingPause");
	REQUIRE(pause != nullptr);
	QMetaObject::invokeMethod(pause, "click");

	window.AddFrames(Frames(4, 9));
	CHECK(window.History().SampleCount() == 3);
	CHECK(window.IsPaused());

	QMetaObject::invokeMethod(pause, "click");
	window.AddFrames(Frames(4, 9));
	CHECK(window.History().SampleCount() == 9);
	CHECK_FALSE(window.IsPaused());
}
