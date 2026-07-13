#include "Thumbnails/TexturePreviewCache.h"

#include "util/QtSupport.h"

#include <QSignalSpy>
#include <QTemporaryDir>

namespace
{
	/** A decoded preview, as a worker would hand one back. */
	QImage
	Preview(int dimension = 64)
	{
		QImage image(dimension, dimension, QImage::Format_RGBA8888);
		image.fill(Qt::red);
		return image;
	}

	/** A temporary directory with real files in it, so FileStamp has something to read. */
	struct Sandbox
	{
		QTemporaryDir temp;

		QString
		WriteTexture(const QString& name) const
		{
			const QString path = temp.filePath(name);

			QFile file(path);
			file.open(QIODevice::WriteOnly);
			file.write("not really a ktx2");
			file.close();

			return path;
		}
	};
}

TEST_CASE("A file that is not there has no stamp", "[thumbnails]")
{
	const Sandbox sandbox;

	REQUIRE(TexturePreviewCache::FileStamp(sandbox.temp.filePath("gone.ktx2")) == 0);
}

TEST_CASE("A file that is there does", "[thumbnails]")
{
	const Sandbox sandbox;

	REQUIRE(TexturePreviewCache::FileStamp(sandbox.WriteTexture("stamped.ktx2")) > 0);
}

TEST_CASE("A preview cache starts empty", "[thumbnails]")
{
	const TexturePreviewCache cache;

	REQUIRE(cache.Lookup("Textures/albedo.ktx2").isNull());
}

TEST_CASE("A delivered preview is cached and announced", "[thumbnails]")
{
	const Sandbox sandbox;

	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &TexturePreviewCache::PreviewReady);

	const QString path = sandbox.WriteTexture("albedo.ktx2");
	cache.Deliver(path, Preview(), TexturePreviewCache::FileStamp(path));

	REQUIRE(!cache.Lookup(path).isNull());

	REQUIRE(ready.count() == 1);
	REQUIRE(ready.front().at(0).toString() == path);
}

TEST_CASE("A preview goes stale once its file changes", "[thumbnails]")
{
	const Sandbox sandbox;

	TexturePreviewCache cache;

	const QString path = sandbox.WriteTexture("albedo.ktx2");

	// Cached against a stamp that is not the file's. That is exactly the state a texture rebaked
	// since it was decoded ends up in, and the cache has to notice rather than show the old image for
	// the rest of the session.
	cache.Deliver(path, Preview(), TexturePreviewCache::FileStamp(path) - 1);

	REQUIRE(cache.Lookup(path).isNull());
}

TEST_CASE("A failed decode caches nothing and announces nothing", "[thumbnails]")
{
	const Sandbox sandbox;

	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &TexturePreviewCache::PreviewReady);

	const QString path = sandbox.WriteTexture("broken.ktx2");

	// A null image is how a worker reports that it could not decode the file. Caching that would be
	// caching a failure.
	cache.Deliver(path, QImage(), TexturePreviewCache::FileStamp(path));

	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE(ready.count() == 0);
}

TEST_CASE("Requesting nothing does nothing", "[thumbnails]")
{
	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &TexturePreviewCache::PreviewReady);

	// A node with no texture assigned asks for one every time it is redrawn.
	cache.Request("");

	REQUIRE(ready.count() == 0);
	REQUIRE(cache.Lookup("").isNull());
}
