#include "Windows/ContentExplorer/ContentExplorerWindow.h"

#include "Project/Project.h"
#include "util/QtSupport.h"

#include <QDragEnterEvent>
#include <QFileSystemModel>
#include <QListView>
#include <QMimeData>
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

	QListView*
	Files(const ContentExplorerWindow& window)
	{
		return window.findChild<QListView*>("CurrentDirectoryExplorer");
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

	QString
	Touch(const Sandbox& sandbox, const QString& relative)
	{
		const fs::path path = sandbox.DataRoot() / relative.toStdString();

		fs::create_directories(path.parent_path());
		std::ofstream(path).put('\0');

		return QString::fromStdString(path.string());
	}

	/**
	 * No other window is holding an asset open, so every deletion is judged on what is on disk alone.
	 * Said out loud because the explorer will not be built without an answer: the guard it feeds is one
	 * that must not be possible to leave unwired.
	 */
	ContentExplorerWindow::AssetsHeldOpenFn
	NothingOpen()
	{
		return [] { return QStringList(); };
	}

	/** The model's index for `path`, once it has scanned far enough to have one. */
	QModelIndex
	IndexFor(QFileSystemModel& model, const QString& path)
	{
		QModelIndex index;
		WaitFor([&] {
			index = model.index(path);
			return index.isValid();
		});

		return index;
	}
}

TEST_CASE("A content explorer with no project has nothing to show", "[contentexplorer]")
{
	const ContentExplorerWindow window(nullptr, NothingOpen());

	// Until a project is open there is nothing to browse, and a browser with no root would only
	// invite the user to click on something that cannot work.
	REQUIRE(!window.isEnabled());
	REQUIRE(Hierarchy(window)->model() == nullptr);
	REQUIRE(Files(window)->model() == nullptr);
}

TEST_CASE("A root path gives the content explorer something to show", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());

	window.SetRootPath(sandbox.DataRootPath());

	REQUIRE(window.isEnabled());
	REQUIRE(Hierarchy(window)->model() != nullptr);
	REQUIRE(Files(window)->model() != nullptr);
}

TEST_CASE("The content explorer is rooted at the project's data directory", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());

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
	ContentExplorerWindow window(nullptr, NothingOpen());

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
	ContentExplorerWindow window(nullptr, NothingOpen());
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
	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	REQUIRE(!AcceptsFile(window, file));
}

TEST_CASE("A drag carrying no files is refused", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	QMimeData text;
	text.setText("stone_wall.glb");

	// Named like a mesh, but there is no file behind it.
	REQUIRE(!AcceptsDrag(window, text));
}

TEST_CASE("A mixed drag is accepted for the mesh in it", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	QMimeData mime;
	mime.setUrls(
		{ QUrl::fromLocalFile("C:/Assets/notes.txt"), QUrl::fromLocalFile("C:/Assets/tree.glb") });

	// One importable file in the selection is enough; the rest are passed over on the drop.
	REQUIRE(AcceptsDrag(window, mime));
}

TEST_CASE("A right-clicked asset resolves to its path under the data root", "[contentexplorer]")
{
	// What the whole deletion hangs on. It is taken while the clicked index is certainly valid, because
	// every dialog that follows runs an event loop, and the model's scanning thread invalidates indices
	// from under one. Lifted out of the menu handler because a QMenu cannot be driven from a test.
	struct Case
	{
		const char* file;
		const char* asset;  // empty: no Delete is offered
	};

	const auto sample = GENERATE(
		Case{ "Meshes/tree.bmesh", "Meshes/tree.bmesh" },
		Case{ "Materials/kirk/Body.bmaterial", "Materials/kirk/Body.bmaterial" },
		Case{ "Textures/basecolor_700a22db7b7ef785.ktx2",
	          "Textures/basecolor_700a22db7b7ef785.ktx2" },
		Case{ "textures_src/kirk/tex0.ktx2", "textures_src/kirk/tex0.ktx2" },

		// Deleting these is not this window's business, whatever their suffix suggests.
		Case{ "Meshes/notes.txt", "" },
		Case{ "Meshes/tree.glb", "" },  // importable, but not yet an asset of the project
		Case{ "Meshes/tree.BMESH", "Meshes/tree.BMESH" });  // the suffix decides, case and all

	INFO("file: " << sample.file);

	const Sandbox sandbox;
	const QString path = Touch(sandbox, sample.file);

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QString asset =
		ContentExplorerWindow::AssetAt(model, IndexFor(model, path), sandbox.DataRootPath());

	CHECK(asset == QString(sample.asset));
}

TEST_CASE("The directories the project is scaffolded with cannot be deleted", "[contentexplorer]")
{
	// Every asset path in the project is written against this layout, and Project::Open puts a missing
	// category straight back -- so deleting one would not even stick.
	const QString category = GENERATE(
		QString("Meshes"),
		QString("Textures"),
		QString("textures_src"),
		QString("Materials"),
		QString("Levels"));

	INFO("category: " << category);

	const Sandbox sandbox;

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex index = IndexFor(model, sandbox.DataRootPath() + "/" + category);

	REQUIRE(model.isDir(index));
	CHECK(ContentExplorerWindow::AssetAt(model, index, sandbox.DataRootPath()).isEmpty());
}

TEST_CASE("A folder the user made is theirs to delete", "[contentexplorer]")
{
	const Sandbox sandbox;

	// The folder an import extracts a mesh's textures into, which is where a project's sources live.
	Touch(sandbox, "textures_src/kirk/tex0.ktx2");

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex index = IndexFor(model, sandbox.DataRootPath() + "/textures_src/kirk");

	REQUIRE(model.isDir(index));
	CHECK(
		ContentExplorerWindow::AssetAt(model, index, sandbox.DataRootPath()) ==
		QString("textures_src/kirk"));

	SECTION("but a click that landed on no row at all is not")
	{
		CHECK(
			ContentExplorerWindow::AssetAt(model, QModelIndex(), sandbox.DataRootPath()).isEmpty());
	}
}

TEST_CASE("A file outside the project is not an asset of it", "[contentexplorer]")
{
	// The path a deletion carries is relative to the data root, and a file above it has no such path.
	// Deleting one would be reaching out of the project the explorer is rooted at.
	const Sandbox sandbox;

	const fs::path outside = sandbox.DataRoot().parent_path() / "stray.bmesh";
	std::ofstream(outside).put('\0');

	QFileSystemModel model;
	model.setRootPath(QString::fromStdString(sandbox.DataRoot().parent_path().string()));

	const QModelIndex index = IndexFor(model, QString::fromStdString(outside.string()));

	CHECK(ContentExplorerWindow::AssetAt(model, index, sandbox.DataRootPath()).isEmpty());
}
