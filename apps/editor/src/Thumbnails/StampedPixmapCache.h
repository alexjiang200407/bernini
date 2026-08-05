#pragma once

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

/**
 * A pixmap per file path, kept only while the file still has the modification time it was made from,
 * and produced at most once at a time.
 *
 * The half TexturePreviewCache and AssetThumbnailCache have in common. A subclass supplies the
 * production -- a decode on a worker for one, a GPU render for the other -- claiming a path with
 * BeginRequest and ending the claim with Store or Abandon. A path stays claimed for everything in
 * between, which is what stops a repaint part-way through from starting the same work again: a miss
 * on Lookup does not mean nothing is coming.
 *
 * Every failure path owes an Abandon. A claim that is never ended is never producible again.
 */
class StampedPixmapCache : public QObject
{
	Q_OBJECT

public:
	// Modification time of `path` in ms, or 0 when it cannot be read.
	[[nodiscard]] static qint64
	FileStamp(const QString& path);

	// The pixmap for `path`, or a null one when it is absent, still being produced, or stale because
	// the file changed on disk since it was made.
	[[nodiscard]] QPixmap
	Lookup(const QString& path) const;

Q_SIGNALS:
	void
	Ready(const QString& path, const QPixmap& pixmap);

protected:
	explicit StampedPixmapCache(int budgetKb, QObject* parent = nullptr);

	/**
	 * Claims `path` for production, having established there is nothing current to serve.
	 *
	 * The stamp comes back rather than being read again at the end: a file rewritten while it was
	 * being produced is then stored under the older stamp, so the next Lookup sees it as stale and
	 * produces it again rather than caching what the file used to hold forever.
	 *
	 * @return The stamp to hand to Store, or nothing when the path is already cached current or
	 *         already claimed -- in which case the caller must start no work.
	 */
	[[nodiscard]] std::optional<qint64>
	BeginRequest(const QString& path);

	// Caches `pixmap` under `path`, ends the claim, and announces it.
	void
	Store(const QString& path, const QPixmap& pixmap, qint64 stamp);

	// Ends the claim with nothing cached, so a later request may try again.
	void
	Abandon(const QString& path) noexcept;

	// Drops every cached pixmap and every claim.
	void
	Clear();

	[[nodiscard]] bool
	IsClaimed(const QString& path) const noexcept;

private:
	struct Entry
	{
		QPixmap pixmap;
		qint64  stamp = 0;
	};

	mutable QCache<QString, Entry> m_Cache;
	QSet<QString>                  m_Claimed;
};
