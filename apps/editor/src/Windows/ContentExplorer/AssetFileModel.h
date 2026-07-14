#pragma once

#include <QFileSystemModel>

class AssetThumbnailCache;

/**
 * The Content Explorer grid's model: a QFileSystemModel that illustrates the asset kinds the editor
 * can draw.
 *
 * QFileSystemModel only offers the OS shell icon. A `.bmesh` and a `.bmaterial` get their rendered
 * thumbnail instead -- looked up on every paint, requested on the first miss, and filled in later,
 * because a render cannot be made to happen inside data(). Everything else (folders, textures)
 * keeps the shell icon.
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

	QVariant
	data(const QModelIndex& index, int role) const override;

private:
	// Repaints the one tile whose thumbnail just arrived.
	void
	OnThumbnailReady(const QString& path);

	AssetThumbnailCache* m_Thumbnails = nullptr;
};
