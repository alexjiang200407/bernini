#include "util/import_outputs.h"

#include "StoreAt.h"

#include <assetlib/import_document.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
	/** Writes `document` as `Authored/Meshes/<stem>.bimport`, beside a `.glb` standing in for the
	 *  source it describes. Returns the source's absolute path, which is what a caller resolves. */
	QString
	WriteImport(
		const QString&                  dataRoot,
		const QString&                  stem,
		const assetlib::ImportDocument& document)
	{
		const QDir root(dataRoot);
		root.mkpath(QStringLiteral("Authored/Meshes"));

		SaveAt(
			document,
			std::filesystem::path(
				root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".bimport")
					.toStdString()));

		const QString source = root.filePath(QStringLiteral("Authored/Meshes/") + stem + ".glb");
		QFile         glb(source);
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.write("source");
		glb.close();

		return source;
	}

	/** Forces `path`'s modification time. QFile can only set it while the file is open. */
	void
	SetModified(const QString& path, const QDateTime& when)
	{
		QFile file(path);
		REQUIRE(file.open(QIODevice::ReadWrite));
		REQUIRE(file.setFileTime(when, QFileDevice::FileModificationTime));
	}

	/** Writes an empty file at `dataRoot`-relative `key`, making its folder. */
	void
	Touch(const QString& dataRoot, const QString& key)
	{
		const QDir root(dataRoot);
		root.mkpath(QFileInfo(key).path());

		QFile file(root.filePath(key));
		REQUIRE(file.open(QIODevice::WriteOnly));
	}
}

TEST_CASE("An imported source is named by its extension alone", "[importoutputs]")
{
	CHECK(editor::IsImportedSource("/tmp/Coyote.glb"));
	CHECK(editor::IsImportedSource("/tmp/Coyote.GLB"));
	CHECK(editor::IsImportedSource("/tmp/cha800_00.reduced.glb"));

	CHECK_FALSE(editor::IsImportedSource("/tmp/Coyote.bimport"));
	CHECK_FALSE(editor::IsImportedSource("/tmp/Coyote.gltf"));
	CHECK_FALSE(editor::IsImportedSource({}));
}

TEST_CASE("A source resolves to what its document says it produced", "[importoutputs]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto document       = assetlib::ImportDocument();
	document.outputs    = { "Derived/Animations/AdaWong/kirk.banim",
		                    "Derived/Meshes/AdaWong/kirk.bmesh",
		                    "Derived/Skeletons/AdaWong/kirk.bskel" };
	document.textureDir = "Derived/SourceTextures/AdaWong";

	const QString source = WriteImport(root, "kirk", document);

	const editor::ImportOutputs outputs = editor::ImportOutputsOf(root, source);

	// The mesh is picked out of `outputs` by what kind of asset it is, not by its position: the
	// list is sorted by key, so the `.banim` leads it.
	CHECK(outputs.mesh == QDir(root).filePath("Derived/Meshes/AdaWong/kirk.bmesh"));
	CHECK(outputs.textureDir == QDir(root).filePath("Derived/SourceTextures/AdaWong"));
}

TEST_CASE("A stem carrying dots keeps every part of itself", "[importoutputs]")
{
	// `cha800_00.reduced.glb` is a real name in the test project, and only the last extension is
	// the extension -- swapping the wrong one looks for a document that does not exist.
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto document    = assetlib::ImportDocument();
	document.outputs = { "Derived/Meshes/cha800_00.reduced.bmesh" };

	const QString source = WriteImport(root, "cha800_00.reduced", document);

	CHECK(
		editor::ImportOutputsOf(root, source).mesh ==
		QDir(root).filePath("Derived/Meshes/cha800_00.reduced.bmesh"));
}

TEST_CASE("A source with nothing behind it resolves to nothing", "[importoutputs]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	SECTION("no document beside it")
	{
		const QDir dir(root);
		dir.mkpath(QStringLiteral("Authored/Meshes"));
		QFile glb(dir.filePath("Authored/Meshes/loose.glb"));
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.close();

		CHECK(
			editor::ImportOutputsOf(root, dir.filePath("Authored/Meshes/loose.glb"))
				.mesh.isEmpty());
	}

	SECTION("a document that will not parse")
	{
		const QDir dir(root);
		dir.mkpath(QStringLiteral("Authored/Meshes"));
		QFile broken(dir.filePath("Authored/Meshes/torn.bimport"));
		REQUIRE(broken.open(QIODevice::WriteOnly));
		broken.write("<<<< merge left unresolved");
		broken.close();

		QFile glb(dir.filePath("Authored/Meshes/torn.glb"));
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.close();

		CHECK(
			editor::ImportOutputsOf(root, dir.filePath("Authored/Meshes/torn.glb")).mesh.isEmpty());
	}

	SECTION("outputs naming no mesh")
	{
		auto document    = assetlib::ImportDocument();
		document.outputs = { "Derived/Skeletons/rig.bskel" };

		CHECK(editor::ImportOutputsOf(root, WriteImport(root, "rigonly", document)).mesh.isEmpty());
	}

	SECTION("an import that extracted no textures")
	{
		auto document    = assetlib::ImportDocument();
		document.outputs = { "Derived/Meshes/bare.bmesh" };

		const QString source = WriteImport(root, "bare", document);
		CHECK(editor::ImportOutputsOf(root, source).textureDir.isEmpty());
		CHECK(editor::ImportTexturesOf(root, source).isEmpty());
	}

	SECTION("a file that is not a source at all")
	{
		CHECK(
			editor::ImportOutputsOf(root, QDir(root).filePath("Authored/Materials/m.bmaterial"))
				.mesh.isEmpty());
	}

	SECTION("no project open")
	{
		CHECK(editor::ImportOutputsOf({}, "/tmp/kirk.glb").mesh.isEmpty());
	}
}

TEST_CASE("An import's textures are read off the folder, not the document", "[importoutputs]")
{
	// A `.ktx2` carries no cache key of its own and is never listed in `outputs` -- the folder is
	// the only statement of which textures an import wrote.
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto document       = assetlib::ImportDocument();
	document.outputs    = { "Derived/Meshes/kirk.bmesh" };
	document.textureDir = "Derived/SourceTextures/kirk";

	const QString source = WriteImport(root, "kirk", document);

	Touch(root, QStringLiteral("Derived/SourceTextures/kirk/normal.ktx2"));
	Touch(root, QStringLiteral("Derived/SourceTextures/kirk/basecolor.ktx2"));
	Touch(root, QStringLiteral("Derived/SourceTextures/kirk/notes.txt"));

	const QStringList textures = editor::ImportTexturesOf(root, source);

	REQUIRE(textures.size() == 2);
	CHECK(textures[0] == QDir(root).filePath("Derived/SourceTextures/kirk/basecolor.ktx2"));
	CHECK(textures[1] == QDir(root).filePath("Derived/SourceTextures/kirk/normal.ktx2"));
}

TEST_CASE("The cache follows the document it answered from", "[importoutputs]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto document    = assetlib::ImportDocument();
	document.outputs = { "Derived/Meshes/kirk.bmesh" };

	const QString source = WriteImport(root, "kirk", document);

	auto cache = editor::ImportOutputsCache();
	cache.SetDataRoot(root);

	REQUIRE(cache.Of(source).mesh == QDir(root).filePath("Derived/Meshes/kirk.bmesh"));

	// A reimport into a different folder rewrites the document, and the answer has to move with it.
	// The stamp is the document's modification time, so the rewrite is made to land on a later
	// millisecond rather than trusting two writes in a row to differ.
	document.outputs = { "Derived/Meshes/moved/kirk.bmesh" };
	const QString documentPath =
		QDir(root).filePath(QStringLiteral("Authored/Meshes/kirk.bimport"));
	SaveAt(document, std::filesystem::path(documentPath.toStdString()));
	SetModified(documentPath, QDateTime::currentDateTime().addSecs(1));

	CHECK(cache.Of(source).mesh == QDir(root).filePath("Derived/Meshes/moved/kirk.bmesh"));

	// A new root is a different project; nothing resolved against the last one still applies. The
	// folder is *made* first: a relative path climbs out of a root with `../`, which resolves fine
	// as long as every directory it climbs through exists -- so an absent one would pass this for
	// the wrong reason and hide exactly the bug it is here to catch.
	const QString elsewhere = QDir(root).filePath("elsewhere");
	REQUIRE(QDir().mkpath(elsewhere));

	cache.SetDataRoot(elsewhere);
	CHECK(cache.Of(source).mesh.isEmpty());
}

TEST_CASE("A source belonging to another project resolves to nothing", "[importoutputs]")
{
	// Two projects side by side, each with a `kirk` import. Resolving one project's source against
	// the other's root must answer nothing -- not the other project's mesh. `QDir::relativeFilePath`
	// will happily climb out with `../`, and `QDir::filePath` reattaches that without cleaning it,
	// so the containment has to be checked rather than assumed.
	QTemporaryDir temp;
	REQUIRE(temp.isValid());

	const QString mine   = QDir(temp.path()).filePath("mine");
	const QString theirs = QDir(temp.path()).filePath("theirs");

	auto document    = assetlib::ImportDocument();
	document.outputs = { "Derived/Meshes/kirk.bmesh" };

	const QString source = WriteImport(mine, "kirk", document);

	auto other    = assetlib::ImportDocument();
	other.outputs = { "Derived/Meshes/not-yours.bmesh" };
	WriteImport(theirs, "kirk", other);

	CHECK(
		editor::ImportOutputsOf(mine, source).mesh ==
		QDir(mine).filePath("Derived/Meshes/kirk.bmesh"));
	CHECK(editor::ImportOutputsOf(theirs, source).mesh.isEmpty());
	CHECK(editor::ImportTexturesOf(theirs, source).isEmpty());

	// A source nowhere near any project answers nothing rather than climbing to find out.
	CHECK(
		editor::ImportOutputsOf(mine, QDir(temp.path()).filePath("loose/kirk.glb")).mesh.isEmpty());
}

TEST_CASE("The cache answers without re-reading the document", "[importoutputs]")
{
	// Without this the test above would pass on a cache that never cached: re-reading every time
	// gets the same answers. Holding the modification time still across a rewrite is the only way
	// to see which of the two happened -- and a stale answer here is the cost the stamp buys, the
	// same one StampedPixmapCache carries.
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto document    = assetlib::ImportDocument();
	document.outputs = { "Derived/Meshes/kirk.bmesh" };

	const QString source = WriteImport(root, "kirk", document);
	const QString documentPath =
		QDir(root).filePath(QStringLiteral("Authored/Meshes/kirk.bimport"));
	const QDateTime stamp = QFileInfo(documentPath).lastModified();

	auto cache = editor::ImportOutputsCache();
	cache.SetDataRoot(root);
	REQUIRE(cache.Of(source).mesh == QDir(root).filePath("Derived/Meshes/kirk.bmesh"));

	document.outputs = { "Derived/Meshes/rewritten.bmesh" };
	SaveAt(document, std::filesystem::path(documentPath.toStdString()));
	SetModified(documentPath, stamp);

	CHECK(cache.Of(source).mesh == QDir(root).filePath("Derived/Meshes/kirk.bmesh"));
	CHECK(
		editor::ImportOutputsOf(root, source).mesh ==
		QDir(root).filePath("Derived/Meshes/rewritten.bmesh"));
}
