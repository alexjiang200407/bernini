#include "Windows/ContentExplorer/AssetFileModel.h"

#include "Thumbnails/AssetThumbnailCache.h"
#include "Thumbnails/TexturePreviewCache.h"
#include "util/asset_paths.h"
#include "util/source_mesh.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <qabstractitemmodel.h>
#include <qfilesystemmodel.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpixmap.h>
#include <qtmetamacros.h>
#include <qvariant.h>

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

void
AssetFileModel::SetDataRoot(const QString& dataRoot)
{
	m_SourceMeshes.SetDataRoot(dataRoot);
	m_SourceForSubject.clear();
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

QString
AssetFileModel::SubjectOf(const QString& path) const
{
	if (!editor::IsImportedSource(path))
		return path;

	const QString mesh = m_SourceMeshes.Of(path);
	if (!mesh.isEmpty())
		m_SourceForSubject.insert(mesh, path);

	return mesh;
}

QVariant
AssetFileModel::data(const QModelIndex& index, int role) const
{
	// A directory is never an asset, whatever its name ends in.
	if ((role != Qt::DecorationRole && role != Qt::ToolTipRole) || index.column() != 0 ||
	    isDir(index))
		return QFileSystemModel::data(index, role);

	const QString row  = filePath(index);
	const QString path = SubjectOf(row);
	if (path.isEmpty())
		return QFileSystemModel::data(index, role);

	StampedPixmapCache* cache = CacheFor(path);
	if (cache == nullptr)
		return QFileSystemModel::data(index, role);

	if (const QString rejection = cache->GetRejection(path); !rejection.isEmpty())
	{
		if (role == Qt::ToolTipRole)
			// Which file failed is the whole message for a source: its own bytes are fine, and a
			// container it produced is rebuilt rather than repaired -- the opposite of what
			// "cannot be read" on a `.glb` would have a person conclude about their model.
			return path == row ? tr("Cannot be read: %1").arg(rejection) :
			                     tr("The mesh it produced cannot be read: %1").arg(rejection);
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
AssetFileModel::Repaint(const QString& path)
{
	const QModelIndex changed = index(path);
	if (changed.isValid())
		Q_EMIT dataChanged(changed, changed, { Qt::DecorationRole, Qt::ToolTipRole });
}

void
AssetFileModel::OnThumbnailChanged(const QString& path)
{
	Repaint(path);

	// A `.bmesh` rendered for a source's tile: the row that asked is the `.glb`, and once the
	// explorer stops showing the derived half it is the only row there is to repaint.
	if (const auto source = m_SourceForSubject.constFind(path); source != m_SourceForSubject.cend())
		Repaint(*source);
}
