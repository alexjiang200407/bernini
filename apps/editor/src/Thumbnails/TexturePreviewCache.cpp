#include "Thumbnails/TexturePreviewCache.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QPainter>
#include <QRunnable>

#include <assetlib/image_io.h>

namespace
{
	QPixmap
	ToDisplayPixmap(const QImage& image)
	{
		constexpr int       c_Cell = 8;
		static const QColor c_Light(0x60, 0x60, 0x60);
		static const QColor c_Dark(0x4a, 0x4a, 0x4a);

		QPixmap  pixmap(image.size());
		QPainter painter(&pixmap);

		for (int y = 0; y < image.height(); y += c_Cell)
		{
			for (int x = 0; x < image.width(); x += c_Cell)
			{
				const bool light = ((x / c_Cell) + (y / c_Cell)) % 2 == 0;
				painter.fillRect(x, y, c_Cell, c_Cell, light ? c_Light : c_Dark);
			}
		}

		painter.drawImage(0, 0, image);
		return pixmap;
	}

	class DecodeTask : public QRunnable
	{
	public:
		DecodeTask(TexturePreviewCache* cache, QString path, qint64 stamp) :
			m_Cache(cache), m_Path(std::move(path)), m_Stamp(stamp)
		{
			setAutoDelete(true);
		}

		void
		run() override
		{
			QImage decoded;
			try
			{
				const auto image = assetlib::loadKTX2Preview(
					std::filesystem::path(m_Path.toStdWString()),
					static_cast<uint32_t>(TexturePreviewCache::c_PreviewDim));

				// deep-copy subresource before `image` leaves scope.
				decoded = QImage(
							  reinterpret_cast<const uchar*>(image.pixels.data()),
							  static_cast<int>(image.width),
							  static_cast<int>(image.height),
							  static_cast<qsizetype>(image.subresources.front().rowPitch),
							  QImage::Format_RGBA8888)
				              .copy();
			}
			catch (const std::exception& e)
			{
				qWarning(
					"TexturePreviewCache: cannot preview '%s': %s",
					qPrintable(m_Path),
					e.what());
			}

			QMetaObject::invokeMethod(
				m_Cache,
				[cache = m_Cache, path = m_Path, decoded, stamp = m_Stamp]() {
					cache->Deliver(path, decoded, stamp);
				},
				Qt::QueuedConnection);
		}

	private:
		TexturePreviewCache* m_Cache = nullptr;
		QString              m_Path;
		qint64               m_Stamp = 0;
	};
}

TexturePreviewCache::TexturePreviewCache(QObject* parent) : QObject(parent)
{
	// Decodes are memory-hungry (a transcoded mip chain of a 4K map is tens of MB); a couple in
	// flight keeps drag-drop responsive without thrashing.
	m_Pool.setMaxThreadCount(2);
}

qint64
TexturePreviewCache::FileStamp(const QString& path)
{
	const QFileInfo info(path);
	return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
}

QPixmap
TexturePreviewCache::Lookup(const QString& path) const
{
	const CachedPreview* entry = m_Cache.object(path);
	if (entry == nullptr || entry->stamp != FileStamp(path))
		return {};

	return entry->pixmap;
}

void
TexturePreviewCache::Request(const QString& path)
{
	if (path.isEmpty() || m_InFlight.contains(path))
		return;

	const qint64 stamp = FileStamp(path);

	const CachedPreview* entry = m_Cache.object(path);
	if (entry != nullptr)
	{
		if (entry->stamp == stamp)
			return;

		// Rebaked on disk since we decoded it. The editor is also the asset-cook host, so this is
		// reachable without ever closing the material.
		m_Cache.remove(path);
	}

	m_InFlight.insert(path);
	m_Pool.start(new DecodeTask(this, path, stamp));
}

void
TexturePreviewCache::Deliver(const QString& path, const QImage& image, qint64 stamp)
{
	m_InFlight.remove(path);

	if (image.isNull())
		return;

	const QPixmap preview = ToDisplayPixmap(image);

	const int costKb = std::max(
		1,
		static_cast<int>(
			(static_cast<qint64>(preview.width()) * preview.height() * preview.depth() / 8) /
			1024));

	m_Cache.insert(path, new CachedPreview{ preview, stamp }, costKb);
	Q_EMIT PreviewReady(path, preview);
}
