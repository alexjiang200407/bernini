#include "Windows/ContentExplorer/AssetFileModel.h"

#include "Thumbnails/TexturePreviewCache.h"
#include "util/QtSupport.h"
#include "util/asset_paths.h"

#include "StoreAt.h"

#include <assetlib/import_document.h>

#include <QDir>
#include <QIcon>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace
{
	using editor::test::WaitFor;

	/** A temporary directory with real files in it, so the model and FileStamp have something to read. */
	struct Sandbox
	{
		QTemporaryDir temp;

		QString
		WriteFile(const QString& name) const
		{
			const QString path = temp.filePath(name);

			QFile file(path);
			file.open(QIODevice::WriteOnly);
			file.write("not really a ktx2");
			file.close();

			return path;
		}
	};

	/** A decoded preview, as the cache would deliver one. */
	QImage
	Preview(int dimension = 64)
	{
		QImage image(dimension, dimension, QImage::Format_RGBA8888);
		image.fill(Qt::red);
		return image;
	}

	/**
	 * A whole import under `root`: `Authored/Meshes/<stem>.glb`, the `.bimport` naming the mesh it
	 * produced, and the `Derived/Meshes/<stem>.bmesh` itself. Returns the source and the mesh.
	 */
	std::pair<QString, QString>
	WriteImport(const QString& root, const QString& stem)
	{
		const QDir dir(root);
		REQUIRE(dir.mkpath(QStringLiteral("Authored/Meshes")));
		REQUIRE(dir.mkpath(QStringLiteral("Derived/Meshes")));

		const QString mesh   = dir.filePath("Derived/Meshes/" + stem + ".bmesh");
		const QString source = dir.filePath("Authored/Meshes/" + stem + ".glb");

		auto document    = assetlib::ImportDocument();
		document.outputs = { ("Derived/Meshes/" + stem + ".bmesh").toStdString() };
		SaveAt(
			document,
			std::filesystem::path(
				dir.filePath("Authored/Meshes/" + stem + ".bimport").toStdString()));

		for (const QString* file : { &source, &mesh })
		{
			QFile handle(*file);
			REQUIRE(handle.open(QIODevice::WriteOnly));
			handle.write("stand-in");
		}

		return { source, mesh };
	}

	/** The model's tile for `path`, once its directory scan has produced one. */
	QModelIndex
	TileFor(AssetFileModel& model, const QString& path)
	{
		REQUIRE(WaitFor([&] { return model.index(path).isValid(); }));
		return model.index(path);
	}
}

TEST_CASE("A texture tile shows its preview once it lands", "[thumbnails]")
{
	const Sandbox sandbox;
	const QString path = sandbox.WriteFile("albedo.ktx2");

	AssetFileModel      model;
	TexturePreviewCache previews;
	model.SetTexturePreviews(&previews);
	model.setRootPath(sandbox.temp.path());

	const QModelIndex tile = TileFor(model, path);

	// The first paint misses and claims the file; the shell icon stands in meanwhile.
	REQUIRE(model.data(tile, Qt::DecorationRole).isValid());

	QSignalSpy repaint(&model, &QAbstractItemModel::dataChanged);
	previews.Deliver(path, Preview(), editor::FileStamp(path));

	const bool tileRepainted = std::ranges::any_of(repaint, [&](const QList<QVariant>& emission) {
		return emission.at(0).toModelIndex() == tile;
	});
	REQUIRE(tileRepainted);

	const QIcon icon = model.data(tile, Qt::DecorationRole).value<QIcon>();
	REQUIRE(icon.pixmap(64).toImage().convertToFormat(QImage::Format_RGBA8888) == Preview());
}

TEST_CASE("A tile that is not a texture never wears one", "[thumbnails]")
{
	const Sandbox sandbox;
	const QString path = sandbox.WriteFile("notes.txt");

	AssetFileModel      model;
	TexturePreviewCache previews;
	model.SetTexturePreviews(&previews);
	model.setRootPath(sandbox.temp.path());

	// Cached under the file's own path, so only the routing keeps it off the tile.
	previews.Deliver(path, Preview(), editor::FileStamp(path));

	const QIcon icon = model.data(TileFor(model, path), Qt::DecorationRole).value<QIcon>();
	REQUIRE(icon.pixmap(64).toImage().convertToFormat(QImage::Format_RGBA8888) != Preview());
}

TEST_CASE("Without a preview cache a texture keeps the shell icon", "[thumbnails]")
{
	const Sandbox sandbox;
	const QString path = sandbox.WriteFile("albedo.ktx2");

	AssetFileModel model;
	model.setRootPath(sandbox.temp.path());

	const QIcon icon = model.data(TileFor(model, path), Qt::DecorationRole).value<QIcon>();
	REQUIRE(icon.pixmap(64).toImage().convertToFormat(QImage::Format_RGBA8888) != Preview());
}

TEST_CASE("A source's tile is repainted by the mesh its import produced", "[thumbnails]")
{
	// Nothing can render a `.glb`, and once the explorer shows only the authored half it is the
	// only row on screen -- so the thumbnail the caches announce is a `.bmesh` nothing is showing,
	// and the tile that has to be repainted is the source's.
	const Sandbox sandbox;
	const QString root        = sandbox.temp.path();
	const auto [source, mesh] = WriteImport(root, "kirk");

	AssetFileModel      model;
	TexturePreviewCache previews;
	model.SetTexturePreviews(&previews);
	model.SetDataRoot(root);
	model.setRootPath(QDir(root).filePath("Authored/Meshes"));

	const QModelIndex tile = TileFor(model, source);

	// Painting is what resolves the source, so the model learns which row the mesh stands for.
	REQUIRE(model.data(tile, Qt::DecorationRole).isValid());

	QSignalSpy repaint(&model, &QAbstractItemModel::dataChanged);
	previews.Deliver(mesh, Preview(), editor::FileStamp(mesh));

	const bool tileRepainted = std::ranges::any_of(repaint, [&](const QList<QVariant>& emission) {
		return emission.at(0).toModelIndex() == tile;
	});
	CHECK(tileRepainted);
}

TEST_CASE("A source nothing resolves stays on its shell icon", "[thumbnails]")
{
	const Sandbox sandbox;
	const QString root        = sandbox.temp.path();
	const auto [source, mesh] = WriteImport(root, "kirk");

	AssetFileModel      model;
	TexturePreviewCache previews;
	model.SetTexturePreviews(&previews);

	SECTION("because no project is open")
	{
		// SetDataRoot is never called, which is what a closed project leaves behind.
	}

	SECTION("because the source belongs to another project")
	{
		const QString elsewhere = QDir(root).filePath("elsewhere");
		REQUIRE(QDir().mkpath(elsewhere));
		model.SetDataRoot(elsewhere);
	}

	model.setRootPath(QDir(root).filePath("Authored/Meshes"));
	const QModelIndex tile = TileFor(model, source);
	REQUIRE(model.data(tile, Qt::DecorationRole).isValid());

	// Nothing recorded the mesh as standing for this row, so its thumbnail repaints nothing.
	QSignalSpy repaint(&model, &QAbstractItemModel::dataChanged);
	previews.Deliver(mesh, Preview(), editor::FileStamp(mesh));

	const bool tileRepainted = std::ranges::any_of(repaint, [&](const QList<QVariant>& emission) {
		return emission.at(0).toModelIndex() == tile;
	});
	CHECK_FALSE(tileRepainted);
}
