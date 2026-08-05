#include "Thumbnails/StampedPixmapCache.h"

#include "util/asset_paths.h"

StampedPixmapCache::StampedPixmapCache(int budgetKb, QObject* parent) :
	QObject(parent), m_Cache(budgetKb)
{}

QPixmap
StampedPixmapCache::Lookup(const QString& path) const
{
	const Entry* entry = m_Cache.object(path);
	if (entry == nullptr || entry->stamp != editor::FileStamp(path))
		return {};

	return entry->pixmap;
}

std::optional<qint64>
StampedPixmapCache::BeginRequest(const QString& path)
{
	if (path.isEmpty() || m_Claimed.contains(path))
		return std::nullopt;

	const qint64 stamp = editor::FileStamp(path);

	if (const Entry* entry = m_Cache.object(path); entry != nullptr)
	{
		if (entry->stamp == stamp)
			return std::nullopt;

		// Rewritten on disk since it was made. The editor is also the asset-cook host, so this is
		// reachable without ever closing the folder.
		m_Cache.remove(path);
	}

	m_Claimed.insert(path);
	return stamp;
}

void
StampedPixmapCache::Store(const QString& path, const QPixmap& pixmap, qint64 stamp)
{
	m_Claimed.remove(path);

	if (pixmap.isNull())
		return;

	const int costKb = std::max(
		1,
		static_cast<int>(
			(static_cast<qint64>(pixmap.width()) * pixmap.height() * pixmap.depth() / 8) / 1024));

	m_Cache.insert(path, new Entry{ pixmap, stamp }, costKb);
	Q_EMIT Ready(path, pixmap);
}

void
StampedPixmapCache::Abandon(const QString& path) noexcept
{
	m_Claimed.remove(path);
}

void
StampedPixmapCache::Clear()
{
	m_Cache.clear();
	m_Claimed.clear();
}

bool
StampedPixmapCache::IsClaimed(const QString& path) const noexcept
{
	return m_Claimed.contains(path);
}
