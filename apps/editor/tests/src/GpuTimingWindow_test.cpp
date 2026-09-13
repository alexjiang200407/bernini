#include "Windows/GpuTiming/GpuTimingWindow.h"
#include "util/held_open_assets.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
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
	class CaptureAssets : public QObject, public editor::IHoldsAssets
	{
	public:
		explicit CaptureAssets(QObject* parent) : QObject(parent) {}

		QStringList paths;

		[[nodiscard]] QStringList
		GetHeldOpenPaths() const override
		{
			return paths;
		}
	};

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
	window.SetSource("Material Editor");

	window.AddFrames(Frames(1, 5));

	CHECK(window.History().SampleCount() == 5);
}

TEST_CASE("A different viewport forgets the frames of the one before", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Material Editor");
	window.AddFrames(Frames(1, 5));

	window.SetSource("Animation Editor");

	// One viewport's frame cost says nothing about another's, and the two would be plotted as one
	// series if the history carried over.
	CHECK(window.History().SampleCount() == 0);
}

TEST_CASE("The same viewport reported again keeps the history", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Material Editor");
	window.AddFrames(Frames(1, 5));

	window.SetSource("Material Editor");

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

TEST_CASE("An export writes every retained frame, and nothing else", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Material Editor");
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

	// A context record precedes the timing header and one row per recorded frame.
	QFile csv(written.filePath(file));
	REQUIRE(csv.open(QIODevice::ReadOnly | QIODevice::Text));
	CHECK(QString::fromUtf8(csv.readAll()).split('\n', Qt::SkipEmptyParts).size() == 32);
}

TEST_CASE("Export retains the latest 3600 samples beyond the visible graph", "[gputiming]")
{
	editor::GpuTimingWindow window;
	window.SetSource("Animation Editor");
	window.AddFrames(Frames(1, 4000));
	REQUIRE(window.History().SampleCount() == 3600);
	CHECK(window.History().FrameAt(0) == 401);
	CHECK(window.History().FrameAt(3599) == 4000);

	const QTemporaryDir directory;
	REQUIRE(directory.isValid());
	const QString file = window.Export(QDir(directory.path()));
	REQUIRE_FALSE(file.isEmpty());
	QFile csv(QDir(directory.path()).filePath(file));
	REQUIRE(csv.open(QIODevice::ReadOnly | QIODevice::Text));
	const QStringList rows = QString::fromUtf8(csv.readAll()).split('\n', Qt::SkipEmptyParts);
	REQUIRE(rows.size() == 3602);
	CHECK(rows.at(2).startsWith("0,401,"));
	CHECK(rows.back().startsWith("3599,4000,"));
}

TEST_CASE("CSV identifies the timing tab and editor assets at export time", "[gputiming]")
{
	QWidget editorRoot;
	editorRoot.setWindowTitle("Bernini — Test Project");
	CaptureAssets assets(&editorRoot);
	assets.paths = { "/Data/old.bmesh" };
	editor::GpuTimingWindow window(&editorRoot);
	window.SetSource("Animation, \"Preview\"");
	window.AddFrames(Frames(1, 3));
	auto* pause = window.findChild<QWidget*>("GpuTimingPause");
	REQUIRE(pause != nullptr);
	QMetaObject::invokeMethod(pause, "click");
	const QString mesh = "/Data/人物,\"hero\"\nLOD.bmesh";
	assets.paths       = { mesh, "/Data/walk.banim", mesh };

	const QTemporaryDir directory;
	REQUIRE(directory.isValid());
	const QString file = window.Export(QDir(directory.path()));
	QFile         csv(QDir(directory.path()).filePath(file));
	REQUIRE(csv.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString first  = QString::fromUtf8(csv.readLine()).trimmed();
	const QString prefix = "# export_context_json,\"";
	REQUIRE(first.startsWith(prefix));
	REQUIRE(first.endsWith('"'));
	QString json = first.mid(prefix.size(), first.size() - prefix.size() - 1);
	json.replace("\"\"", "\"");
	const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
	REQUIRE(document.isObject());
	const QJsonObject context = document.object();
	CHECK(context["snapshot"].toString() == "export_time");
	CHECK(context["timing_tab"].toString() == "Animation, \"Preview\"");
	CHECK(context["recording_paused"].toBool());
	CHECK(context["editor_title"].toString() == editorRoot.windowTitle());
	CHECK_FALSE(context["exported_at_utc"].toString().isEmpty());
	CHECK(context["open_assets_scope"].toString() == "all_editor_panels");
	const QJsonArray open = context["open_assets"].toArray();
	CHECK(open.size() == 2);
	CHECK(open.contains(mesh));
	CHECK(open.contains("/Data/walk.banim"));
	CHECK_FALSE(open.contains("/Data/old.bmesh"));
	CHECK(csv.readLine().startsWith("sample,frame,"));
	CHECK(csv.readLine().startsWith("0,1,"));
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
	window.SetSource("Material Editor");
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
