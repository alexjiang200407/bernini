#include "Windows/ContentExplorer/AssetFileModel.h"

#include "Thumbnails/AssetThumbnailCache.h"

#include <QIcon>

AssetFileModel::AssetFileModel(QObject* parent) : QFileSystemModel(parent) {}

void
AssetFileModel::SetThumbnails(AssetThumbnailCache* thumbnails)
{
	if (m_Thumbnails != nullptr)
		disconnect(m_Thumbnails, nullptr, this, nullptr);

	m_Thumbnails = thumbnails;

	if (m_Thumbnails == nullptr)
		return;

	connect(
		m_Thumbnails,
		&AssetThumbnailCache::ThumbnailReady,
		this,
		[this](const QString& path, const QPixmap&) { OnThumbnailReady(path); });
}

QVariant
AssetFileModel::data(const QModelIndex& index, int role) const
{
	if (role != Qt::DecorationRole || index.column() != 0 || m_Thumbnails == nullptr)
		return QFileSystemModel::data(index, role);

	const QString path = filePath(index);
	if (!AssetThumbnailCache::CanThumbnail(path))
		return QFileSystemModel::data(index, role);

	if (const QPixmap thumbnail = m_Thumbnails->Lookup(path); !thumbnail.isNull())
		return QIcon(thumbnail);

	// A miss: the shell icon stands in until the render lands and OnThumbnailReady repaints the tile.
	m_Thumbnails->Request(path);
	return QFileSystemModel::data(index, role);
}

void
AssetFileModel::OnThumbnailReady(const QString& path)
{
	const QModelIndex changed = index(path);
	if (changed.isValid())
		Q_EMIT dataChanged(changed, changed, { Qt::DecorationRole });
}
