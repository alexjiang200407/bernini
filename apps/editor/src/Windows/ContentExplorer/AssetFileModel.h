#pragma once

#include <QFileSystemModel>
#include <QHash>
#include <qabstractitemmodel.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qvariant.h>

#include "util/source_mesh.h"

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
 * An **imported source** is the one row illustrated by a file other than itself. A `.glb` is what a
 * person sees and nothing can render one, so its tile wears the thumbnail of the `.bmesh` its import
 * produced -- which is also the file whose mtime decides whether that thumbnail is still current.
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

	/**
	 * The project's Data directory, which an imported source is resolved against. Empty (the
	 * default, and what a closed project means) leaves every `.glb` on its shell icon.
	 */
	void
	SetDataRoot(const QString& dataRoot);

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

	/**
	 * The file whose thumbnail illustrates the row at `path` -- itself, except for an imported
	 * source, which its mesh illustrates. Empty when the source produced no mesh or no project is
	 * open, which leaves the row on its shell icon.
	 *
	 * Records the reverse, because a cache announces the path it *rendered*: a thumbnail arriving
	 * for a `.bmesh` has to repaint the `.glb` tile that asked for it, and that tile is the only
	 * one on screen.
	 */
	[[nodiscard]] QString
	SubjectOf(const QString& path) const;

	// Repaints the tile at `path`, if it is one this model has a row for.
	void
	Repaint(const QString& path);

	// Repaints whichever tile the thumbnail that just arrived -- or just failed -- belongs to.
	void
	OnThumbnailChanged(const QString& path);

	AssetThumbnailCache* m_Thumbnails      = nullptr;
	TexturePreviewCache* m_TexturePreviews = nullptr;

	editor::SourceMeshCache m_SourceMeshes;

	/**
	 * Subject path -> the source row it illustrates. Only imported sources are in it.
	 *
	 * Bounded by the sources a session paints, and cleared when the project changes. An entry whose
	 * source has since been reimported onto a *different* mesh is left behind rather than pruned:
	 * finding it would mean scanning the map on a paint, and the cost of keeping it is one spurious
	 * repaint of a row that then re-derives its subject correctly anyway.
	 */
	mutable QHash<QString, QString> m_SourceForSubject;
};
