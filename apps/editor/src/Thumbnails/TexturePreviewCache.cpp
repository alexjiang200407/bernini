#include "Thumbnails/TexturePreviewCache.h"

#include <QDebug>
#include <QPainter>
#include <QRunnable>

#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>

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

TexturePreviewCache::TexturePreviewCache(QObject* parent) : StampedPixmapCache(c_BudgetKb, parent)
{
	// Decodes are memory-hungry (a transcoded mip chain of a 4K map is tens of MB); a couple in
	// flight keeps drag-drop responsive without thrashing.
	m_Pool.setMaxThreadCount(2);
}

void
TexturePreviewCache::Request(const QString& path)
{
	const std::optional<qint64> stamp = BeginRequest(path);
	if (!stamp)
		return;

	m_Pool.start(new DecodeTask(this, path, *stamp));
}

void
TexturePreviewCache::Deliver(const QString& path, const QImage& image, qint64 stamp)
{
	if (image.isNull())
	{
		// A baked block format has no CPU decode path at all: the same content fails the same way.
		Reject(path, stamp);
		return;
	}

	Store(path, ToDisplayPixmap(image), stamp);
}
