// <assetlib/codecs.h> carries the AssetCodec<T> specialisations that SaveAt's
// `AssetCodecFor` constraint needs. A template specialisation is not a symbol reference
// include-cleaner can see, so it reads as unused right up until the call stops matching.
#include "util/mesh_drop.h"
#include <assetlib/codecs.h>  // IWYU pragma: keep

#include "StoreAt.h"

#include <assetlib/import_document.h>

#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <qbuffer.h>
#include <qcontainerfwd.h>
#include <qfileinfo.h>
#include <qlist.h>
#include <qobject.h>
#include <qstringliteral.h>

namespace
{
	/** A drag carrying `paths` as local files, in order. */
	void
	SetLocalFiles(QMimeData& mime, const QStringList& paths)
	{
		auto urls = QList<QUrl>();
		for (const QString& path : paths) urls.push_back(QUrl::fromLocalFile(path));

		mime.setUrls(urls);
	}

	/** Writes `Authored/Meshes/<stem>.glb` and the `.bimport` describing it. Returns the source. */
	QString
	WriteSource(const QString& dataRoot, const QString& stem, const QStringList& outputs)
	{
		const QDir root(dataRoot);
		root.mkpath(QStringLiteral("Authored/Meshes"));

		auto document = assetlib::ImportDocument();
		for (const QString& output : outputs) document.outputs.push_back(output.toStdString());

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
}

TEST_CASE("A mesh viewport takes a container or a source", "[drop]")
{
	auto mime = QMimeData();

	SetLocalFiles(mime, { "/tmp/kirk.bmesh" });
	CHECK(editor::IsMeshDrag(&mime));

	SetLocalFiles(mime, { "/tmp/kirk.glb" });
	CHECK(editor::IsMeshDrag(&mime));

	// Case-insensitive, like every other suffix rule the drag filters share.
	SetLocalFiles(mime, { "/tmp/kirk.GLB" });
	CHECK(editor::IsMeshDrag(&mime));

	SECTION("and nothing else")
	{
		SetLocalFiles(mime, { "/tmp/skin.bmaterial", "/tmp/day.benv", "/tmp/kirk.bimport" });
		CHECK_FALSE(editor::IsMeshDrag(&mime));

		// A source's own extension, not glTF in general: only `.glb` is imported.
		SetLocalFiles(mime, { "/tmp/kirk.gltf" });
		CHECK_FALSE(editor::IsMeshDrag(&mime));

		auto empty = QMimeData();
		CHECK_FALSE(editor::IsMeshDrag(&empty));
		CHECK_FALSE(editor::IsMeshDrag(nullptr));
	}

	SECTION("a remote URL is not a file")
	{
		mime.setUrls({ QUrl("https://example.com/kirk.glb") });
		CHECK_FALSE(editor::IsMeshDrag(&mime));
	}
}

TEST_CASE("A dropped source resolves to the mesh it produced", "[drop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	const QString source =
		WriteSource(root, "kirk", { "Derived/Animations/kirk.banim", "Derived/Meshes/kirk.bmesh" });

	auto mime = QMimeData();
	SetLocalFiles(mime, { source });

	CHECK(
		editor::GetMeshDroppedOn(&mime, root).mesh ==
		QDir(root).filePath("Derived/Meshes/kirk.bmesh"));

	SECTION("and needs the project to do it")
	{
		// Without a data root there is no document to read, so the source resolves to nothing --
		// where a container would still have dropped, since it carries its own absolute path.
		CHECK(editor::GetMeshDroppedOn(&mime, {}).mesh.isEmpty());
	}

	SECTION("a source of another project resolves to nothing")
	{
		QTemporaryDir elsewhere;
		REQUIRE(elsewhere.isValid());

		CHECK(editor::GetMeshDroppedOn(&mime, elsewhere.path()).mesh.isEmpty());
	}

	SECTION("a source whose document names no mesh resolves to nothing")
	{
		const QString rigOnly = WriteSource(root, "prop", { "Derived/Skeletons/prop.bskel" });

		auto only = QMimeData();
		SetLocalFiles(only, { rigOnly });

		const editor::MeshDrop drop = editor::GetMeshDroppedOn(&only, root);
		CHECK(drop.mesh.isEmpty());
		CHECK(drop.source == rigOnly);
	}

	SECTION("a source with no document at all resolves to nothing")
	{
		const QString orphan = QDir(root).filePath("Authored/Meshes/orphan.glb");
		QFile         glb(orphan);
		REQUIRE(glb.open(QIODevice::WriteOnly));
		glb.close();

		auto only = QMimeData();
		SetLocalFiles(only, { orphan });

		CHECK(editor::GetMeshDroppedOn(&only, root).mesh.isEmpty());
	}
}

TEST_CASE("A dropped container is taken as itself", "[drop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	auto mime = QMimeData();
	SetLocalFiles(mime, { "/tmp/loose/kirk.bmesh" });

	// Never resolved through a project: a container names the file to read.
	CHECK(editor::GetMeshDroppedOn(&mime, root).mesh == QString("/tmp/loose/kirk.bmesh"));
	CHECK(editor::GetMeshDroppedOn(&mime, {}).mesh == QString("/tmp/loose/kirk.bmesh"));

	SECTION("and wins over a source dragged with it")
	{
		const QString source = WriteSource(root, "kirk", { "Derived/Meshes/kirk.bmesh" });

		SetLocalFiles(mime, { source, "/tmp/loose/other.bmesh" });
		CHECK(editor::GetMeshDroppedOn(&mime, root).mesh == QString("/tmp/loose/other.bmesh"));
	}

	SECTION("nothing droppable resolves to nothing")
	{
		SetLocalFiles(mime, { "/tmp/skin.bmaterial" });
		CHECK(editor::GetMeshDroppedOn(&mime, root).mesh.isEmpty());
		CHECK(editor::GetMeshDroppedOn(nullptr, root).mesh.isEmpty());
	}
}

TEST_CASE("The mesh a source names need not exist yet", "[drop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	// Nothing under Derived/ is written: a `.bmesh` is cache, and a fresh checkout has none until
	// a bake runs. The drop still names it, so the load is what reports it missing.
	const QString source = WriteSource(root, "kirk", { "Derived/Meshes/kirk.bmesh" });

	auto mime = QMimeData();
	SetLocalFiles(mime, { source });

	const QString resolved = editor::GetMeshDroppedOn(&mime, root).mesh;
	CHECK(resolved == QDir(root).filePath("Derived/Meshes/kirk.bmesh"));
	CHECK_FALSE(QFileInfo::exists(resolved));
}

TEST_CASE("A source that resolves to nothing is still named", "[drop]")
{
	QTemporaryDir temp;
	REQUIRE(temp.isValid());
	const QString root = temp.path();

	REQUIRE(QDir(root).mkpath(QStringLiteral("Authored/Meshes")));

	const QString orphan = QDir(root).filePath("Authored/Meshes/orphan.glb");
	QFile         glb(orphan);
	REQUIRE(glb.open(QIODevice::WriteOnly));
	glb.close();

	auto mime = QMimeData();
	SetLocalFiles(mime, { orphan });

	// What separates "nothing was dropped" from "something was, and it had nothing to show". The
	// second is reported to the user; the first is a drag this viewport simply did not want.
	const editor::MeshDrop unresolved = editor::GetMeshDroppedOn(&mime, root);
	CHECK(unresolved.mesh.isEmpty());
	CHECK(unresolved.source == orphan);

	SECTION("where a container names no source, having needed none")
	{
		SetLocalFiles(mime, { "/tmp/loose/kirk.bmesh" });

		const editor::MeshDrop container = editor::GetMeshDroppedOn(&mime, root);
		CHECK(container.mesh == QString("/tmp/loose/kirk.bmesh"));
		CHECK(container.source.isEmpty());
	}

	SECTION("and a drag of neither names nothing at all")
	{
		SetLocalFiles(mime, { "/tmp/skin.bmaterial" });

		const editor::MeshDrop none = editor::GetMeshDroppedOn(&mime, root);
		CHECK(none.mesh.isEmpty());
		CHECK(none.source.isEmpty());
	}
}
