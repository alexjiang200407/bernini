#include "Thumbnails/StampedPixmapCache.h"

#include "util/QtSupport.h"
#include "util/asset_paths.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

namespace
{
	/**
	 * The smallest thing that can drive the base: production is whatever the test does between
	 * BeginRequest and Store, so the claim rules are visible with no worker and no device in the way.
	 */
	class TestCache : public StampedPixmapCache
	{
	public:
		TestCache() : StampedPixmapCache(1024) {}

		using StampedPixmapCache::Abandon;
		using StampedPixmapCache::BeginRequest;
		using StampedPixmapCache::Clear;
		using StampedPixmapCache::IsClaimed;
		using StampedPixmapCache::Reject;
		using StampedPixmapCache::Store;

		void
		Request(const QString&) override
		{}
	};

	QPixmap
	Produced()
	{
		QPixmap pixmap(8, 8);
		pixmap.fill(Qt::red);
		return pixmap;
	}

	/** A temporary directory with real files in it, so FileStamp has something to read. */
	struct Sandbox
	{
		QTemporaryDir temp;

		QString
		Write(const QString& name) const
		{
			const QString path = temp.filePath(name);

			QFile file(path);
			file.open(QIODevice::WriteOnly);
			file.write("content");
			file.close();

			return path;
		}
	};
}

TEST_CASE("A claim outlives the miss it was taken for", "[thumbnails]")
{
	// The bug this pins. Production takes several turns of the event loop, and the view repaints
	// during them -- so Lookup misses while the work is already under way. If the claim ended before
	// the entry landed, that miss would start the whole read and render a second time.
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("apples.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	REQUIRE(cache.IsClaimed(path));

	// A repaint, part-way through: nothing to serve yet, and nothing may be started.
	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE_FALSE(cache.BeginRequest(path).has_value());
	REQUIRE_FALSE(cache.BeginRequest(path).has_value());

	cache.Store(path, Produced(), *stamp);

	REQUIRE_FALSE(cache.IsClaimed(path));
	REQUIRE_FALSE(cache.Lookup(path).isNull());
}

TEST_CASE("A stored pixmap is announced once", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;
	QSignalSpy    ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.Write("apples.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	cache.Store(path, Produced(), *stamp);

	REQUIRE(ready.count() == 1);
	REQUIRE(ready.front().at(0).toString() == path);
}

TEST_CASE("Nothing current is worth producing again", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("apples.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	cache.Store(path, Produced(), *stamp);

	// Cached against the stamp the file still has, so there is nothing to do.
	REQUIRE_FALSE(cache.BeginRequest(path).has_value());
}

TEST_CASE("A file rewritten since it was produced is produced again", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("apples.bmesh");

	// Stored against a stamp that is not the file's -- exactly the state an asset re-baked since it
	// was rendered ends up in. The editor is also the cook host, so this is reachable without ever
	// closing the folder.
	cache.Store(path, Produced(), editor::FileStamp(path) - 1);

	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE(cache.BeginRequest(path).has_value());
}

TEST_CASE("Abandoning a claim lets a later request take it", "[thumbnails]")
{
	// Every failure path owes an Abandon: a read that threw, a render that threw, a capture
	// discarded on the way down. A claim left behind would make that asset unproducible for the rest
	// of the session, which is worse than the failure that caused it.
	const Sandbox sandbox;
	TestCache     cache;
	QSignalSpy    ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.Write("apples.bmesh");

	REQUIRE(cache.BeginRequest(path).has_value());
	cache.Abandon(path);

	REQUIRE_FALSE(cache.IsClaimed(path));
	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE(ready.count() == 0);

	// And the next attempt is allowed to run.
	REQUIRE(cache.BeginRequest(path).has_value());
}

TEST_CASE("Storing nothing caches nothing and announces nothing", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;
	QSignalSpy    ready(&cache, &StampedPixmapCache::Ready);

	const QString path = sandbox.Write("apples.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	cache.Store(path, QPixmap(), *stamp);

	// The claim still ends -- a producer that came back empty is done with the path either way.
	REQUIRE_FALSE(cache.IsClaimed(path));
	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE(ready.count() == 0);
}

TEST_CASE("A rejected file is not produced again until it changes", "[thumbnails]")
{
	// A producer that fails on a file's content will fail on it again -- a baked block-format
	// texture has no CPU decode at all -- and requests arrive from paint, so an Abandon here would
	// re-run the failure on every repaint for the rest of the session.
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("baked.ktx2");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	cache.Reject(path, *stamp);

	REQUIRE_FALSE(cache.IsClaimed(path));
	REQUIRE(cache.Lookup(path).isNull());
	REQUIRE_FALSE(cache.BeginRequest(path).has_value());
}

TEST_CASE("A rejection is forgotten once its file changes", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("baked.ktx2");

	// Rejected under a stamp that is no longer the file's -- a texture re-baked since the failure.
	// New content deserves a new attempt.
	cache.Reject(path, editor::FileStamp(path) - 1);

	REQUIRE(cache.BeginRequest(path).has_value());
}

TEST_CASE("A late rejection does not evict what already landed", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;

	const QString path = sandbox.Write("apples.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(path);
	REQUIRE(stamp.has_value());
	cache.Store(path, Produced(), *stamp);

	// A second producer's failure coming back after a success landed for the same content.
	cache.Reject(path, *stamp);

	REQUIRE_FALSE(cache.Lookup(path).isNull());
}

TEST_CASE("A change of project drops what was cached and what was claimed", "[thumbnails]")
{
	const Sandbox sandbox;
	TestCache     cache;

	const QString stored  = sandbox.Write("stored.bmesh");
	const QString claimed = sandbox.Write("claimed.bmesh");

	const std::optional<qint64> stamp = cache.BeginRequest(stored);
	REQUIRE(stamp.has_value());
	cache.Store(stored, Produced(), *stamp);
	REQUIRE(cache.BeginRequest(claimed).has_value());

	// The same relative path means a different asset under a different data root, and the work
	// already under way was for the old one.
	cache.Clear();

	REQUIRE(cache.Lookup(stored).isNull());
	REQUIRE_FALSE(cache.IsClaimed(claimed));
	REQUIRE(cache.BeginRequest(claimed).has_value());
}

TEST_CASE("An empty path is never claimed", "[thumbnails]")
{
	// A node with no texture assigned asks for one every time it is redrawn.
	TestCache cache;

	REQUIRE_FALSE(cache.BeginRequest("").has_value());
	REQUIRE_FALSE(cache.IsClaimed(""));
}
