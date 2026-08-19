#include "Windows/ContentExplorer/AssetFileModel.h"

#include "Thumbnails/AssetThumbnailCache.h"
#include "Thumbnails/TexturePreviewCache.h"
#include "util/asset_paths.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>

AssetFileModel::AssetFileModel(QObject* parent) : QFileSystemModel(parent) {}

void
AssetFileModel::SetThumbnails(AssetThumbnailCache* thumbnails)
{
	Rebind(m_Thumbnails, thumbnails);
}

void
AssetFileModel::SetTexturePreviews(TexturePreviewCache* previews)
{
	Rebind(m_TexturePreviews, previews);
}

template <typename Cache>
void
AssetFileModel::Rebind(Cache*& member, Cache* to)
{
	if (member != nullptr)
		disconnect(member, nullptr, this, nullptr);

	member = to;

	if (member != nullptr)
	{
		connect(
			member,
			&StampedPixmapCache::Ready,
			this,
			[this](const QString& path, const QPixmap&) { OnThumbnailChanged(path); });
		connect(
			member,
			&StampedPixmapCache::Rejected,
			this,
			[this](const QString& path, const QString&) { OnThumbnailChanged(path); });
	}
}

StampedPixmapCache*
AssetFileModel::CacheFor(const QString& path) const
{
	if (m_Thumbnails != nullptr && AssetThumbnailCache::CanThumbnail(path))
		return m_Thumbnails;

	if (m_TexturePreviews != nullptr && editor::IsTextureFile(path))
		return m_TexturePreviews;

	return nullptr;
}

QVariant
AssetFileModel::data(const QModelIndex& index, int role) const
{
	// A directory is never an asset, whatever its name ends in.
	if ((role != Qt::DecorationRole && role != Qt::ToolTipRole) || index.column() != 0 ||
	    isDir(index))
		return QFileSystemModel::data(index, role);

	const QString       path  = filePath(index);
	StampedPixmapCache* cache = CacheFor(path);
	if (cache == nullptr)
		return QFileSystemModel::data(index, role);

	if (const QString rejection = cache->GetRejection(path); !rejection.isEmpty())
	{
		if (role == Qt::ToolTipRole)
			return tr("Cannot be read: %1").arg(rejection);
		return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
	}

	if (role == Qt::ToolTipRole)
		return QFileSystemModel::data(index, role);

	if (const QPixmap pixmap = cache->Lookup(path); !pixmap.isNull())
		return QIcon(pixmap);

	// A miss: the shell icon stands in until the work lands and OnThumbnailChanged repaints the tile.
	cache->Request(path);
	return QFileSystemModel::data(index, role);
}

void
AssetFileModel::OnThumbnailChanged(const QString& path)
{
	const QModelIndex changed = index(path);
	if (changed.isValid())
		Q_EMIT dataChanged(changed, changed, { Qt::DecorationRole, Qt::ToolTipRole });
}
