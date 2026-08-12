#pragma once

#include <QImage>
#include <QThreadPool>

#include "Thumbnails/StampedPixmapCache.h"

/**
 * Decodes .ktx2 files into small RGBA pixmaps for display in the editor, off the UI thread.
 *
 * Evicting a live preview is safe: QPixmap is implicitly shared, so a node that already applied one
 * keeps its own reference.
 */
class TexturePreviewCache : public StampedPixmapCache
{
	Q_OBJECT

public:
	static constexpr int c_PreviewDim = 256;

	static constexpr int c_BudgetKb = 64 * 1024;

	explicit TexturePreviewCache(QObject* parent = nullptr);

	// Decodes `path` unless a current copy is cached or one is already being decoded. Emits Ready on
	// success.
	void
	Request(const QString& path) override;

	// Hands a finished decode back. Called by a worker via a queued invocation, so it always runs
	// on the UI thread -- QPixmap may only be touched there, which is why the worker yields a
	// QImage. A null `image` means the decode failed, which is remembered until the file changes.
	void
	Deliver(const QString& path, const QImage& image, qint64 stamp);

private:
	QThreadPool m_Pool;
};
