#include "util/source_mesh.h"

#include "util/asset_paths.h"

#include <assetlib/codecs.h>
#include <assetlib/import_document.h>

#include <QDir>

namespace editor
{
	namespace
	{
		QString
		Suffix(const std::string_view extension)
		{
			return QString::fromUtf8(extension.data(), static_cast<qsizetype>(extension.size()));
		}

		/**
		 * The `.bimport` describing the source at `path`, absolute, or empty when it is not an
		 * imported source of this project's.
		 *
		 * Through the key rather than the path: `importDocumentKeyFor` owns the `.glb` -> `.bimport`
		 * rule, and a key is what it takes -- see STYLE.md § Paths for why the two are not
		 * interchangeable.
		 */
		QString
		DocumentFor(const QString& dataRoot, const QString& path)
		{
			if (!IsImportedSource(path) || dataRoot.isEmpty())
				return {};

			const QDir root(dataRoot);

			// A source belonging to another project would otherwise resolve straight back into it;
			// GetKeyUnder is where that is refused, and where the reason is written down.
			const QString key = GetKeyUnder(dataRoot, path);
			if (key.isEmpty() || key == ".")
				return {};

			const QByteArray utf8 = key.toUtf8();
			return root.filePath(
				QString::fromStdString(
					assetlib::importDocumentKeyFor(
						std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())))));
		}
	}

	bool
	IsImportedSource(const QString& path)
	{
		return path.endsWith(Suffix(assetlib::c_ImportedSourceExtension), Qt::CaseInsensitive);
	}

	QString
	GetSourceMesh(const QString& dataRoot, const QString& path)
	{
		const QString document = DocumentFor(dataRoot, path);
		if (document.isEmpty() || !QFileInfo::exists(document))
			return {};

		try
		{
			const assetlib::ImportDocument read =
				assetlib::loadImportDocument(std::filesystem::path(document.toStdString()));

			const std::string mesh = read.GetMeshOutput();

			return mesh.empty() ? QString() : QDir(dataRoot).filePath(QString::fromStdString(mesh));
		}
		catch (const std::exception&)
		{
			// A document from a newer branch, or one caught mid-write. The caller shows the source
			// with nothing resolved, which is what it would show for an unimported `.glb` anyway.
			return {};
		}
	}

	QString
	GetSourceSkeleton(const QString& dataRoot, const QString& path)
	{
		const QString document = DocumentFor(dataRoot, path);
		if (document.isEmpty() || !QFileInfo::exists(document))
			return {};

		try
		{
			return QString::fromStdString(
				assetlib::loadImportDocument(std::filesystem::path(document.toStdString()))
					.skeleton);
		}
		catch (const std::exception&)
		{
			return {};
		}
	}

	void
	SourceMeshCache::SetDataRoot(const QString& dataRoot)
	{
		m_DataRoot = dataRoot;
		m_Entries.clear();
	}

	QString
	SourceMeshCache::Of(const QString& path) const
	{
		const QString document = DocumentFor(m_DataRoot, path);
		if (document.isEmpty())
			return {};

		// Stamped on the document and not on the source: the `.glb` does not change when a reimport
		// writes different containers, and the document is what names them.
		const qint64 stamp = FileStamp(document);

		const auto cached = m_Entries.constFind(path);
		if (cached != m_Entries.cend() && cached->stamp == stamp)
			return cached->mesh;

		const Entry entry = { GetSourceMesh(m_DataRoot, path), stamp };
		m_Entries.insert(path, entry);
		return entry.mesh;
	}
}
