#pragma once

#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QThreadPool>

/**
 * Decodes .ktx2 files into small RGBA pixmaps for display in the editor, off the UI thread.
 *
 * Evicting a live preview is safe: QPixmap is implicitly shared, so a node that already applied one
 * keeps its own reference.
 */
class TexturePreviewCache : public QObject
{
	Q_OBJECT

public:
	// The longer edge of a decoded preview. Larger than any node draws it, so zooming the graph in
	// scales down rather than up.
	static constexpr int c_PreviewDim = 256;

	// Roughly 256 distinct textures at the worst-case 256x256x4 an entry can cost.
	static constexpr int c_BudgetKb = 64 * 1024;

	explicit TexturePreviewCache(QObject* parent = nullptr);

	// The preview for `path`, or a null pixmap when it is absent, still decoding, undecodable, or
	// stale because the file changed on disk since it was decoded.
	[[nodiscard]] QPixmap
	Lookup(const QString& path) const;

	// Decodes `path` unless a current copy is cached or one is already in flight. Emits
	// PreviewReady on success.
	void
	Request(const QString& path);

	// Hands a finished decode back. Called by a worker via a queued invocation, so it always runs
	// on the UI thread -- QPixmap may only be touched there, which is why the worker yields a
	// QImage. A null `image` means the decode failed and only clears the in-flight entry.
	//
	// `stamp` is the file's modification time read *before* the decode began: a file rewritten
	// while it was decoding is then stored under the older stamp, so the next Lookup sees it as
	// stale and decodes again rather than caching the previous contents forever.
	void
	Deliver(const QString& path, const QImage& image, qint64 stamp);

	// Modification time of `path` in ms, or 0 when it cannot be read.
	[[nodiscard]] static qint64
	FileStamp(const QString& path);

Q_SIGNALS:
	void
	PreviewReady(const QString& path, const QPixmap& preview);

private:
	struct CachedPreview
	{
		QPixmap pixmap;
		qint64  stamp = 0;
	};

	mutable QCache<QString, CachedPreview> m_Cache{ c_BudgetKb };

	QSet<QString> m_InFlight;

	QThreadPool m_Pool;
};
