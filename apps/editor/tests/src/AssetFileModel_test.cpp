#include "Windows/ContentExplorer/AssetFileModel.h"

#include "Thumbnails/TexturePreviewCache.h"
#include "util/QtSupport.h"
#include "util/asset_paths.h"

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
