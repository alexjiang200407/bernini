#include "Thumbnails/StampedPixmapCache.h"
#include "Thumbnails/TexturePreviewCache.h"

#include "util/QtSupport.h"
#include "util/asset_paths.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <qbuffer.h>
#include <qdir.h>
#include <qimage.h>
#include <qnamespace.h>
#include <qobject.h>

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

	REQUIRE(editor::FileStamp(sandbox.temp.filePath("gone.ktx2")) == 0);
}

TEST_CASE("A file that is there does", "[thumbnails]")
{
	const Sandbox sandbox;

	REQUIRE(editor::FileStamp(sandbox.WriteTexture("stamped.ktx2")) > 0);
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
	QSignalSpy          ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.WriteTexture("albedo.ktx2");
	cache.Deliver(path, Preview(), editor::FileStamp(path));

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
	cache.Deliver(path, Preview(), editor::FileStamp(path) - 1);

	REQUIRE(cache.Lookup(path).isNull());
}

TEST_CASE("A failed decode caches nothing and announces nothing", "[thumbnails]")
{
	const Sandbox sandbox;

	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.WriteTexture("broken.ktx2");

	// A null image is how a worker reports that it could not decode the file.
	cache.Deliver(path, QImage(), editor::FileStamp(path));

	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE(ready.count() == 0);
}

TEST_CASE("A failed decode is not retried until the file changes", "[thumbnails]")
{
	// The Content Explorer requests from paint, and a baked block-format texture fails its decode
	// deterministically -- so a failure the cache forgot would be re-run on every repaint of the
	// folder, re-reading the file from disk each time.
	class OpenCache : public TexturePreviewCache
	{
	public:
		using StampedPixmapCache::BeginRequest;
	};

	const Sandbox sandbox;
	OpenCache     cache;

	const QString path = sandbox.WriteTexture("baked_bc7.ktx2");
	cache.Deliver(path, QImage(), editor::FileStamp(path));

	REQUIRE_FALSE(cache.BeginRequest(path).has_value());
}

TEST_CASE("A late failed decode does not evict what already landed", "[thumbnails]")
{
	// The preview path's half of E3's gate. Request claims the file and starts a decode of content
	// that is not a KTX2, so the worker will fail -- but a good preview is delivered before it comes
	// back. The failure ends a claim that is already over, and must not take the cached preview with
	// it.
	const Sandbox sandbox;

	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.WriteTexture("albedo.ktx2");

	cache.Request(path);
	cache.Deliver(path, Preview(), editor::FileStamp(path));

	REQUIRE(!cache.Lookup(path).isNull());
	REQUIRE(ready.count() == 1);

	// The worker's own Deliver, with the null image a failed decode yields.
	REQUIRE(editor::test::WaitFor([&] { return cache.Lookup(path).isNull(); }, 1000) == false);
	REQUIRE(ready.count() == 1);
}

TEST_CASE("Requesting nothing does nothing", "[thumbnails]")
{
	TexturePreviewCache cache;
	QSignalSpy          ready(&cache, &StampedPixmapCache::Ready);

	// A node with no texture assigned asks for one every time it is redrawn.
	cache.Request("");

	REQUIRE(ready.count() == 0);
	REQUIRE(cache.Lookup("").isNull());
}
