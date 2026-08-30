#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/ContentExplorer/asset_rules.h"

#include "Thumbnails/TexturePreviewCache.h"
#include "util/QtSupport.h"
#include "util/asset_paths.h"
#include <assetlib/Project.h>

#include <QDir>
#include <QDragEnterEvent>
#include <QFileSystemModel>
#include <QListView>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
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
			assetlib::Project::Create(
				temp.path().toStdString() / fs::path("MyGame") /
					("MyGame" + std::string(assetlib::Project::c_FileExtension)),
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

	QToolButton*
	Back(const ContentExplorerWindow& window)
	{
		return window.findChild<QToolButton*>("BackButton");
	}

	/** The directory the grid is rooted at, or empty before the explorer has a project. */
	QString
	Shown(const ContentExplorerWindow& window)
	{
		auto* files = Files(window);
		auto* model = qobject_cast<QFileSystemModel*>(files->model());

		return model == nullptr ? QString() : model->filePath(files->rootIndex());
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

TEST_CASE("The explorer previews a texture with no outside wiring", "[contentexplorer]")
{
	const Sandbox sandbox;
	const QString texture = Touch(sandbox, "Textures/albedo.ktx2");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto*             model = qobject_cast<QFileSystemModel*>(Files(window)->model());
	const QModelIndex tile  = IndexFor(*model, texture);
	REQUIRE(tile.isValid());

	QImage preview(64, 64, QImage::Format_RGBA8888);
	preview.fill(Qt::red);

	// Delivered through the cache the explorer stands itself -- no MainWindow wiring exists to fake.
	QSignalSpy repaint(model, &QAbstractItemModel::dataChanged);
	window.GetTexturePreviews().Deliver(texture, preview, editor::FileStamp(texture));

	const bool tileRepainted = std::ranges::any_of(repaint, [&](const QList<QVariant>& emission) {
		return emission.at(0).toModelIndex() == tile;
	});
	REQUIRE(tileRepainted);

	const QIcon icon = model->data(tile, Qt::DecorationRole).value<QIcon>();
	REQUIRE(icon.pixmap(64).toImage().convertToFormat(QImage::Format_RGBA8888) == preview);
}

TEST_CASE("The content explorer is rooted at the project's authored half", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());

	window.SetRootPath(sandbox.DataRootPath());

	auto* model = qobject_cast<QFileSystemModel*>(Hierarchy(window)->model());
	REQUIRE(model != nullptr);

	// One level inside the data root, not at it. `Derived/` is not hidden row by row -- it is
	// simply not somewhere the views can navigate, which is a property of where they point.
	REQUIRE(
		QDir(model->filePath(Hierarchy(window)->rootIndex())) ==
		QDir(sandbox.DataRootPath() + "/Authored"));

	// And it fills in, on a worker thread. Four rows: every authored category Project::Create
	// scaffolds -- Meshes, Materials, Environments, Levels -- and nothing derived.
	REQUIRE(WaitFor([&] { return model->rowCount(Hierarchy(window)->rootIndex()) == 4; }));
}

TEST_CASE("The explorer resolves against the data root it is not rooted at", "[contentexplorer]")
{
	// Moving where the views point must not move what a path means: a key is data-root-relative,
	// and every reference stored in the project is written against that. This is the one thing
	// rooting the views one level in could have broken silently.
	const Sandbox sandbox;
	Touch(sandbox, "Authored/Materials/kirk/Body.bmaterial");

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex index =
		IndexFor(model, sandbox.DataRootPath() + "/Authored/Materials/kirk/Body.bmaterial");

	CHECK(
		editor::AssetAt(model, index, sandbox.DataRootPath()) ==
		QString("Authored/Materials/kirk/Body.bmaterial"));
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
		Case{ "Derived/Meshes/tree.bmesh", "Derived/Meshes/tree.bmesh" },
		Case{ "Authored/Materials/kirk/Body.bmaterial", "Authored/Materials/kirk/Body.bmaterial" },
		Case{ "Textures/basecolor_700a22db7b7ef785.ktx2",
	          "Textures/basecolor_700a22db7b7ef785.ktx2" },
		Case{ "Derived/SourceTextures/kirk/tex0.ktx2", "Derived/SourceTextures/kirk/tex0.ktx2" },

		// Deleting these is not this window's business, whatever their suffix suggests.
		Case{ "Derived/Meshes/notes.txt", "" },
		Case{ "Derived/Meshes/tree.glb", "" },  // importable, but not yet an asset of the project
		Case{ "Derived/Meshes/tree.BMESH",
	          "Derived/Meshes/tree.BMESH" });  // the suffix decides, case and all

	INFO("file: " << sample.file);

	const Sandbox sandbox;
	const QString path = Touch(sandbox, sample.file);

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QString asset = editor::AssetAt(model, IndexFor(model, path), sandbox.DataRootPath());

	CHECK(asset == QString(sample.asset));
}

TEST_CASE("Only a material is offered a Bake action", "[contentexplorer]")
{
	// Baking composites a material's routes into its triplet, so it means nothing for a mesh, a texture
	// or a directory -- the menu offers it for a `.bmaterial` alone. By the extension, case and all.
	CHECK(editor::IsMaterialAsset("Authored/Materials/skin.bmaterial"));
	CHECK(editor::IsMaterialAsset("Authored/Materials/skin.BMATERIAL"));

	CHECK_FALSE(editor::IsMaterialAsset("Derived/Meshes/tree.bmesh"));
	CHECK_FALSE(editor::IsMaterialAsset("Textures/base.ktx2"));
	CHECK_FALSE(editor::IsMaterialAsset("Derived/SourceTextures/kirk"));  // a directory
	CHECK_FALSE(editor::IsMaterialAsset(""));
}

TEST_CASE("A file a panel has open is held, and its neighbours are not", "[contentexplorer]")
{
	// The rule every on-disk operation is gated on, lifted out because the dialogs around it are
	// modal. An empty entry is a holder with nothing bound, and must not read as a match.
	const Sandbox sandbox;
	const QString benv = Touch(sandbox, "Authored/Environments/studio.benv");
	const QString sky  = Touch(sandbox, "Derived/Sky/studio.bsky");

	CHECK(editor::IsHeldOpen({ benv }, benv, false));
	CHECK_FALSE(editor::IsHeldOpen({ benv }, sky, false));
	CHECK_FALSE(editor::IsHeldOpen({}, benv, false));
	CHECK_FALSE(editor::IsHeldOpen({ QString() }, benv, false));
}

TEST_CASE("A directory is held by anything open beneath it", "[contentexplorer]")
{
	const Sandbox sandbox;
	const QString benv = Touch(sandbox, "Authored/Environments/studio.benv");
	const QString root = sandbox.DataRootPath();

	CHECK(editor::IsHeldOpen({ benv }, QDir(root).absoluteFilePath("Authored/Environments"), true));

	// Containment, not a prefix match: a sibling whose name starts with the same characters is a
	// different folder and takes nothing with it.
	CHECK_FALSE(
		editor::IsHeldOpen({ benv }, QDir(root).absoluteFilePath("Authored/Environ"), true));
	CHECK_FALSE(editor::IsHeldOpen({ benv }, QDir(root).absoluteFilePath("Derived/Sky"), true));
}

TEST_CASE(
	"A viewport's configured environment is resolved before it is compared",
	"[contentexplorer]")
{
	// config.json names a `.benv` relative to the working directory, so that is how a viewport hands
	// it back. Left unresolved, such a path is inside *every* directory it is tested against:
	// QDir::relativeFilePath hands a relative argument straight back, and nothing in it starts with
	// `..`, so every folder in the project would read as held and none could be deleted.
	const Sandbox sandbox;
	const QString relative = "Authored/Environments/studio.benv";

	CHECK(editor::IsHeldOpen({ relative }, QDir::current().absoluteFilePath(relative), false));
	CHECK(
		editor::IsHeldOpen(
			{ relative },
			QDir::current().absoluteFilePath("Authored/Environments"),
			true));

	CHECK_FALSE(editor::IsHeldOpen({ relative }, sandbox.DataRootPath(), true));
}

TEST_CASE("A rename accepts only names every platform can round-trip", "[contentexplorer]")
{
	// The rule behind the Rename dialog, lifted out because the dialog is modal and cannot be driven
	// from a test. The project's data root is shared across platforms, so the strictest one -- Windows
	// -- decides: its reserved characters, and the trailing dot or space it silently strips. A leading
	// dot is refused because it hides the file on the others.
	CHECK(editor::IsValidAssetFileName("tree"));
	CHECK(editor::IsValidAssetFileName("tree_02 final"));
	CHECK(editor::IsValidAssetFileName("tree_v1.2"));  // a dot inside is a name
	CHECK(editor::IsValidAssetFileName("console"));    // reserved only when exact
	CHECK(editor::IsValidAssetFileName("com10"));      // the devices stop at 9

	CHECK_FALSE(editor::IsValidAssetFileName(""));
	CHECK_FALSE(editor::IsValidAssetFileName(".hidden"));
	CHECK_FALSE(editor::IsValidAssetFileName("tree."));
	CHECK_FALSE(editor::IsValidAssetFileName("tree "));
	CHECK_FALSE(editor::IsValidAssetFileName("a/b"));  // one component, not a path
	CHECK_FALSE(editor::IsValidAssetFileName("a\\b"));
	CHECK_FALSE(editor::IsValidAssetFileName("a:b"));
	CHECK_FALSE(editor::IsValidAssetFileName("a?b"));
	CHECK_FALSE(editor::IsValidAssetFileName(QString("a%1b").arg(QChar(0x07))));
	CHECK_FALSE(editor::IsValidAssetFileName("NUL"));
	CHECK_FALSE(editor::IsValidAssetFileName("nul.ktx2"));
	CHECK_FALSE(editor::IsValidAssetFileName("Com3"));
}

TEST_CASE("The directories the project is scaffolded with cannot be deleted", "[contentexplorer]")
{
	// Every asset path in the project is written against this layout, and assetlib::Project::Open puts a missing
	// category straight back -- so deleting one would not even stick.
	const QString category = GENERATE(
		QString("Authored"),
		QString("Authored/Materials"),
		QString("Authored/Levels"),
		QString("Derived"),
		QString("Derived/Meshes"),
		QString("Derived/BakedTextures"),
		QString("Derived/SourceTextures"));

	INFO("category: " << category);

	const Sandbox sandbox;

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex index = IndexFor(model, sandbox.DataRootPath() + "/" + category);

	REQUIRE(model.isDir(index));
	CHECK(editor::AssetAt(model, index, sandbox.DataRootPath()).isEmpty());
}

TEST_CASE("A folder the user made is theirs to delete", "[contentexplorer]")
{
	const Sandbox sandbox;

	// The folder an import extracts a mesh's textures into, which is where a project's sources live.
	Touch(sandbox, "Derived/SourceTextures/kirk/tex0.ktx2");

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex index =
		IndexFor(model, sandbox.DataRootPath() + "/Derived/SourceTextures/kirk");

	REQUIRE(model.isDir(index));
	CHECK(
		editor::AssetAt(model, index, sandbox.DataRootPath()) ==
		QString("Derived/SourceTextures/kirk"));

	SECTION("but a click that landed on no row at all is not")
	{
		CHECK(editor::AssetAt(model, QModelIndex(), sandbox.DataRootPath()).isEmpty());
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

	CHECK(editor::AssetAt(model, index, sandbox.DataRootPath()).isEmpty());
}

TEST_CASE("Back has nowhere to go until the explorer has been somewhere", "[contentexplorer]")
{
	const Sandbox         sandbox;
	ContentExplorerWindow window(nullptr, NothingOpen());

	window.SetRootPath(sandbox.DataRootPath());

	// Rooted, but the data root is where the explorer starts: there is nothing behind it.
	CHECK(!Back(window)->isEnabled());
}

TEST_CASE("Back returns the grid to the folder shown before", "[contentexplorer]")
{
	const Sandbox sandbox;
	Touch(sandbox, "Authored/Materials/kirk/Body.bmaterial");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto* tree  = Hierarchy(window);
	auto* model = qobject_cast<QFileSystemModel*>(tree->model());
	REQUIRE(model != nullptr);

	const QString folder = sandbox.DataRootPath() + "/Authored/Materials/kirk";
	tree->setCurrentIndex(IndexFor(*model, folder));

	REQUIRE(QDir(Shown(window)) == QDir(folder));
	REQUIRE(Back(window)->isEnabled());

	Back(window)->click();

	CHECK(QDir(Shown(window)) == QDir(sandbox.DataRootPath() + "/Authored"));

	// Back to the start, so there is nothing behind it again.
	CHECK(!Back(window)->isEnabled());
}

TEST_CASE("Back skips a folder that has been deleted since it was shown", "[contentexplorer]")
{
	const Sandbox sandbox;
	Touch(sandbox, "Authored/Materials/kirk/Body.bmaterial");
	Touch(sandbox, "Authored/Materials/spock/Body.bmaterial");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto* tree  = Hierarchy(window);
	auto* model = qobject_cast<QFileSystemModel*>(tree->model());
	REQUIRE(model != nullptr);

	tree->setCurrentIndex(IndexFor(*model, sandbox.DataRootPath() + "/Authored/Materials/kirk"));
	tree->setCurrentIndex(IndexFor(*model, sandbox.DataRootPath() + "/Authored/Materials/spock"));

	// Removed from underneath the editor, as deleting it in Finder would. Nothing pumps the event
	// loop between here and the click, so the model has not been told either -- which is the case
	// this pins: the history is checked against the disk, not against what the model still lists.
	fs::remove_all(sandbox.DataRoot() / "Authored/Materials" / "kirk");

	Back(window)->click();

	CHECK(QDir(Shown(window)) == QDir(sandbox.DataRootPath() + "/Authored"));
}

TEST_CASE("Back moves the tree's selection with the grid", "[contentexplorer]")
{
	const Sandbox sandbox;
	Touch(sandbox, "Authored/Materials/kirk/Body.bmaterial");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto* tree  = Hierarchy(window);
	auto* model = qobject_cast<QFileSystemModel*>(tree->model());
	REQUIRE(model != nullptr);

	const QString parent = sandbox.DataRootPath() + "/Authored/Materials";
	tree->setCurrentIndex(IndexFor(*model, parent));
	tree->setCurrentIndex(IndexFor(*model, parent + "/kirk"));

	Back(window)->click();

	// Left behind, the tree would go on highlighting the folder the grid has left -- and clicking
	// that row again would move nothing, because it is already the current one.
	CHECK(QDir(model->filePath(tree->currentIndex())) == QDir(parent));
}

TEST_CASE("The explorer lists a source but not the document beside it", "[contentexplorer]")
{
	// One row per model. The `.bimport` carries the import settings and the `.glb` is the thing a
	// person recognises, so the sidecar is not listed -- Unity hides a `.meta` and Godot a
	// `.import` for the same reason.
	const Sandbox sandbox;
	Touch(sandbox, "Authored/Meshes/kirk.glb");
	Touch(sandbox, "Authored/Meshes/kirk.bimport");
	Touch(sandbox, "Authored/Meshes/SPOCK.BIMPORT");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto* files = Files(window);
	auto* grid  = qobject_cast<QFileSystemModel*>(files->model());
	REQUIRE(grid != nullptr);

	// Root the grid at Authored/Meshes/ the way the user does: through the tree.
	auto* tree      = Hierarchy(window);
	auto* hierarchy = qobject_cast<QFileSystemModel*>(tree->model());
	REQUIRE(hierarchy != nullptr);
	const QString meshes =
		QString::fromStdString((sandbox.DataRoot() / "Authored/Meshes").string());
	tree->setCurrentIndex(IndexFor(*hierarchy, meshes));
	REQUIRE(WaitFor([&] { return Shown(window) == meshes; }));

	const QModelIndex source   = IndexFor(*grid, meshes + "/kirk.glb");
	const QModelIndex document = IndexFor(*grid, meshes + "/kirk.bimport");
	const QModelIndex upper    = IndexFor(*grid, meshes + "/SPOCK.BIMPORT");

	// Rows arrive asynchronously; the hider runs off rowsInserted, so wait for its verdicts.
	CHECK(WaitFor([&] { return files->isRowHidden(document.row()); }));
	CHECK(WaitFor([&] { return files->isRowHidden(upper.row()); }));
	CHECK_FALSE(files->isRowHidden(source.row()));

	// The tree hides them too, under the expanded folder.
	tree->expand(IndexFor(*hierarchy, meshes));
	const QModelIndex treeDocument = IndexFor(*hierarchy, meshes + "/kirk.bimport");
	CHECK(WaitFor([&] { return tree->isRowHidden(treeDocument.row(), treeDocument.parent()); }));
	const QModelIndex treeSource = IndexFor(*hierarchy, meshes + "/kirk.glb");
	CHECK_FALSE(tree->isRowHidden(treeSource.row(), treeSource.parent()));
}

TEST_CASE("A derived file is not a person's to rename or delete", "[contentexplorer]")
{
	// The views no longer reach these at all, which is what makes this unreachable in practice --
	// and exactly why it is written down rather than left to be a consequence of where a view is
	// rooted. Losing an authored file loses work; a derived one is a bake's to write back.
	CHECK_FALSE(editor::IsActionableAsset("Derived/Meshes/unit.bmesh"));
	CHECK_FALSE(editor::IsActionableAsset("Derived/Skeletons/rig.bskel"));
	CHECK_FALSE(editor::IsActionableAsset("Derived/SourceTextures/kirk/tex0.ktx2"));
	CHECK_FALSE(editor::IsActionableAsset("Derived/Sky/studio.bsky"));

	// A directory under it is refused the same way: what is in it is what it would take.
	CHECK_FALSE(editor::IsActionableAsset("Derived/SourceTextures/kirk"));

	CHECK(editor::IsActionableAsset("Authored/Materials/skin.bmaterial"));
	CHECK(editor::IsActionableAsset("Authored/Meshes/kirk.glb"));
	CHECK(editor::IsActionableAsset("Authored/Environments/studio.benv"));
	CHECK(editor::IsActionableAsset("Authored/Materials/kirk"));

	// Normalized first, so a key cannot dodge the rule by spelling its way out and back in.
	CHECK_FALSE(editor::IsActionableAsset("Authored/../Derived/Meshes/unit.bmesh"));

	CHECK_FALSE(editor::IsActionableAsset({}));
}

TEST_CASE("The derived half is not somewhere the views can be sent", "[contentexplorer]")
{
	// setRootIndex limits what a view *draws*, not what setCurrentIndex may select, and
	// currentChanged navigates -- so the model still holds an index for every derived directory,
	// and a programmatic selection would re-root the grid onto one. That is the shape a later
	// "reveal in explorer", a search box or a drop handler would arrive in.
	const Sandbox sandbox;
	Touch(sandbox, "Derived/SourceTextures/kirk/tex0.ktx2");
	Touch(sandbox, "Authored/Materials/kirk/Body.bmaterial");

	ContentExplorerWindow window(nullptr, NothingOpen());
	window.SetRootPath(sandbox.DataRootPath());

	auto* tree  = Hierarchy(window);
	auto* model = qobject_cast<QFileSystemModel*>(tree->model());
	REQUIRE(model != nullptr);

	const QString authored = sandbox.DataRootPath() + "/Authored";
	REQUIRE(QDir(Shown(window)) == QDir(authored));

	SECTION("a derived folder selected in the tree moves nothing")
	{
		const QString derived = sandbox.DataRootPath() + "/Derived/SourceTextures/kirk";
		tree->setCurrentIndex(IndexFor(*model, derived));

		CHECK(QDir(Shown(window)) == QDir(authored));
		CHECK(!Back(window)->isEnabled());
	}

	SECTION("nor does the data root above it")
	{
		tree->setCurrentIndex(IndexFor(*model, sandbox.DataRootPath()));

		CHECK(QDir(Shown(window)) == QDir(authored));
		CHECK(!Back(window)->isEnabled());
	}

	SECTION("and an authored folder still moves the grid")
	{
		const QString materials = sandbox.DataRootPath() + "/Authored/Materials/kirk";
		tree->setCurrentIndex(IndexFor(*model, materials));

		CHECK(WaitFor([&] { return QDir(Shown(window)) == QDir(materials); }));
		CHECK(Back(window)->isEnabled());
	}
}

TEST_CASE("A folder whose name begins with dots is still the project's", "[contentexplorer]")
{
	// The bug the one containment answer fixes. AssetAt rejected on a bare `startsWith("..")`,
	// which is a test on the *key*, not on its first component -- so a folder at the data root
	// named `..hidden` read as a climb out of the project and was unactionable: no rename, no
	// delete, for a folder the user made and can see.
	const Sandbox sandbox;
	Touch(sandbox, "..hidden/Body.bmaterial");

	QFileSystemModel model;
	model.setRootPath(sandbox.DataRootPath());

	const QModelIndex folder = IndexFor(model, sandbox.DataRootPath() + "/..hidden");
	REQUIRE(model.isDir(folder));

	CHECK(editor::AssetAt(model, folder, sandbox.DataRootPath()) == QString("..hidden"));

	// The climb itself is still refused: a folder named `..` is not a folder at all.
	CHECK(
		editor::AssetAt(model, IndexFor(model, sandbox.DataRootPath()), sandbox.DataRootPath())
			.isEmpty());
}

TEST_CASE("A held file is held whatever its name contains", "[contentexplorer]")
{
	// The regression the naive convergence would have caused, and the reason KeyUnder is not
	// IsContainedRelativePath: this gate is what stops a Delete going through while a panel still
	// has the file open. A name it read as "outside its own folder" would open that gate.
	const Sandbox sandbox;
	const QString root = sandbox.DataRootPath();

	const QString awkward = Touch(sandbox, "Authored/Materials/a:b.bmaterial");
	const QString dotted  = Touch(sandbox, "Authored/Materials/..hidden/Body.bmaterial");

	const QString materials = QDir(root).absoluteFilePath("Authored/Materials");

	CHECK(editor::IsHeldOpen({ awkward }, materials, true));
	CHECK(editor::IsHeldOpen({ dotted }, materials, true));

	// The folder still holds itself, so deleting it takes what is open inside it.
	CHECK(editor::IsHeldOpen({ materials }, materials, true));

	// And something genuinely elsewhere is still not held.
	CHECK_FALSE(
		editor::IsHeldOpen(
			{ QDir(root).absoluteFilePath("Authored/Environments/studio.benv") },
			materials,
			true));
}
