#include "Windows/ContentExplorer/ContentExplorerWindow.h"

#include "Project/Project.h"
#include "util/QtSupport.h"

#include <QDragEnterEvent>
#include <QFileSystemModel>
#include <QMimeData>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeView>

namespace
{
	namespace fs = std::filesystem;

	using editor::test::WaitFor;

	/** A scaffolded project on disk, and a content explorer with nothing pointed at it yet. */
	struct Sandbox
	{
		QTemporaryDir temp;

		Sandbox()
		{
			Project::Create(
				temp.path().toStdString() / fs::path("MyGame") /
					("MyGame" + std::string(Project::c_FileExtension)),
				"MyGame");
		}

		fs::path
		DataRoot() const
		{
			return temp.path().toStdString() / fs::path("MyGame") / "Data";
		}

		QString
		DataRootPath() const
		{
			return QString::fromStdString(DataRoot().string());
		}
	};

	QTreeView*
	Hierarchy(const ContentExplorerWindow& window)
	{
		return window.findChild<QTreeView*>("FileExplorer");
	}

	QTableView*
	Files(const ContentExplorerWindow& window)
	{
		return window.findChild<QTableView*>("CurrentDirectoryExplorer");
	}

	/** Whether the window would take this drag. */
	bool
	AcceptsDrag(ContentExplorerWindow& window, const QMimeData& mime)
	{
		QDragEnterEvent
			enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);

		// The reject path returns without calling ignore(), and a QDragEnterEvent arrives accepted, so
		// a test that does not clear it first cannot tell the two apart.
		enter.ignore();
		QCoreApplication::sendEvent(&window, &enter);

		return enter.isAccepted();
	}

	/** A drag carrying one local file. */
	bool
	AcceptsFile(ContentExplorerWindow& window, const QString& path)
	{
		QMimeData mime;
		mime.setUrls({ QUrl::fromLocalFile(path) });

		return AcceptsDrag(window, mime);
	}
}

TEST_CASE("A content explorer with no project has nothing to show", "[contentexplorer]")
{
	const ContentExplorerWindow window;

	// Until a project is open there is nothing to browse, and a browser with no root would only
	// invite the user to click on something that cannot work.
	REQUIRE(!window.isEnabled());
	REQUIRE(Hierarchy(window)->model() == nullptr);
	REQUIRE(Files(window)->model() == nullptr);
}

TEST_CASE("A root path gives the content explorer something to show", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window;

	window.SetRootPath(sandbox.DataRootPath());

	REQUIRE(window.isEnabled());
	REQUIRE(Hierarchy(window)->model() != nullptr);
	REQUIRE(Files(window)->model() != nullptr);
}

TEST_CASE("The content explorer is rooted at the project's data directory", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window;

	window.SetRootPath(sandbox.DataRootPath());

	auto* model = qobject_cast<QFileSystemModel*>(Hierarchy(window)->model());
	REQUIRE(model != nullptr);

	// It shows the project's Data tree and nothing above it: the rest of the disk is not the
	// project's business.
	REQUIRE(QDir(model->filePath(Hierarchy(window)->rootIndex())) == QDir(sandbox.DataRootPath()));

	// And it fills in, on a worker thread. Project::Create scaffolds Meshes, Textures, textures_src,
	// Materials and Levels.
	REQUIRE(WaitFor([&] { return model->rowCount(Hierarchy(window)->rootIndex()) == 5; }));
}

TEST_CASE("Files are dragged out of the explorer rather than moved", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window;

	window.SetRootPath(sandbox.DataRootPath());

	// Dragging a texture onto the material graph must copy a reference to it, not pick the file up
	// and carry it out of the project.
	REQUIRE(Files(window)->dragDropMode() == QAbstractItemView::DragOnly);
	REQUIRE(Hierarchy(window)->dragDropMode() == QAbstractItemView::DragOnly);
}

TEST_CASE("A mesh dragged onto the explorer is accepted", "[contentexplorer]")
{
	// The suffix decides, case-insensitively -- a file's name has nothing to do with what it is.
	const QString file = GENERATE(
		QString("C:/Assets/tree.glb"),
		QString("C:/Assets/tree.gltf"),
		QString("C:/Assets/tree.GLB"),
		QString("C:/Assets/tree.GlTf"));

	INFO("dragged: " << file);

	const Sandbox         sandbox;
	ContentExplorerWindow window;
	window.SetRootPath(sandbox.DataRootPath());

	REQUIRE(AcceptsFile(window, file));
}

TEST_CASE("Anything the importer cannot read is refused", "[contentexplorer]")
{
	// Accepting the drag would promise an import that cannot happen.
	const QString file = GENERATE(
		QString("C:/Assets/tree.bmesh"),  // already cooked
		QString("C:/Assets/bark.ktx2"),   // a texture
		QString("C:/Assets/tree.obj"),
		QString("C:/Assets/tree.fbx"),
		QString("C:/Assets/tree"));  // no suffix at all

	INFO("dragged: " << file);

	const Sandbox         sandbox;
	ContentExplorerWindow window;
	window.SetRootPath(sandbox.DataRootPath());

	REQUIRE(!AcceptsFile(window, file));
}

TEST_CASE("A drag carrying no files is refused", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window;
	window.SetRootPath(sandbox.DataRootPath());

	QMimeData text;
	text.setText("stone_wall.glb");

	// Named like a mesh, but there is no file behind it.
	REQUIRE(!AcceptsDrag(window, text));
}

TEST_CASE("A mixed drag is accepted for the mesh in it", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window;
	window.SetRootPath(sandbox.DataRootPath());

	QMimeData mime;
	mime.setUrls(
		{ QUrl::fromLocalFile("C:/Assets/notes.txt"), QUrl::fromLocalFile("C:/Assets/tree.glb") });

	// One importable file in the selection is enough; the rest are passed over on the drop.
	REQUIRE(AcceptsDrag(window, mime));
}
