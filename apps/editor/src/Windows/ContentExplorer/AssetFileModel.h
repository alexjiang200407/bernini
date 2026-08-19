#pragma once

#include <QFileSystemModel>

class AssetThumbnailCache;
class StampedPixmapCache;
class TexturePreviewCache;

/**
 * The Content Explorer grid's model: a QFileSystemModel that illustrates the asset kinds the editor
 * can draw.
 *
 * QFileSystemModel only offers the OS shell icon. A `.bmesh` and a `.bmaterial` get their rendered
 * thumbnail instead, and a texture its decoded pixels -- looked up on every paint, requested on the
 * first miss, and filled in later, because neither a render nor a decode can be made to happen
 * inside data(). Everything else (folders and files the editor cannot draw) keeps the shell icon.
 * An asset the editor cannot read shows a warning in place of its thumbnail and says why in its
 * tooltip -- the container's own message, so a file from a newer engine or one no rule can convert
 * is a thing you can see and act on, rather than a tile that stays on the shell icon forever.
 *
 * A QFileSystemModel subclass rather than a proxy: the views index straight into this model in a
 * dozen places, and a proxy would put a mapToSource in front of every one of them for nothing.
 */
class AssetFileModel : public QFileSystemModel
{
	Q_OBJECT

public:
	explicit AssetFileModel(QObject* parent = nullptr);

	// Null (the default) disables mesh thumbnails, leaving every tile on its shell icon.
	void
	SetThumbnails(AssetThumbnailCache* thumbnails);

	// Null (the default) disables texture previews, leaving texture tiles on their shell icon.
	void
	SetTexturePreviews(TexturePreviewCache* previews);

	QVariant
	data(const QModelIndex& index, int role) const override;

private:
	// Moves this model's Ready subscription onto `to` and makes it `member`'s new value; either may
	// be null.
	template <typename Cache>
	void
	Rebind(Cache*& member, Cache* to);

	// The cache that illustrates `path`, or null when nothing does.
	[[nodiscard]] StampedPixmapCache*
	CacheFor(const QString& path) const;

	// Repaints the one tile whose thumbnail just arrived, or whose read just failed.
	void
	OnThumbnailChanged(const QString& path);

	AssetThumbnailCache* m_Thumbnails      = nullptr;
	TexturePreviewCache* m_TexturePreviews = nullptr;
};
