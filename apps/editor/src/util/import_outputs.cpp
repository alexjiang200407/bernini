#include "util/import_outputs.h"

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
		 * The `.bimport` describing `source`, absolute, or empty when `source` is not an imported
		 * source of this project's.
		 *
		 * Through the key rather than the path: `importDocumentKeyFor` owns the `.glb` -> `.bimport`
		 * rule, and a key is what it takes -- see STYLE.md § Paths for why the two are not
		 * interchangeable.
		 */
		QString
		DocumentFor(const QString& dataRoot, const QString& source)
		{
			if (!IsImportedSource(source) || dataRoot.isEmpty())
				return {};

			const QDir root(dataRoot);

			// A source belonging to another project would otherwise resolve straight back into it;
			// GetKeyUnder is where that is refused, and where the reason is written down.
			const QString key = GetKeyUnder(dataRoot, source);
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

	ImportOutputs
	ImportOutputsOf(const QString& dataRoot, const QString& source)
	{
		const QString document = DocumentFor(dataRoot, source);
		if (document.isEmpty() || !QFileInfo::exists(document))
			return {};

		try
		{
			const assetlib::ImportDocument read =
				assetlib::loadImportDocument(std::filesystem::path(document.toStdString()));

			const QDir root(dataRoot);

			auto outputs = ImportOutputs();
			if (const std::string mesh = assetlib::meshOutputOf(read); !mesh.empty())
				outputs.mesh = root.filePath(QString::fromStdString(mesh));

			if (!read.textureDir.empty())
				outputs.textureDir = root.filePath(QString::fromStdString(read.textureDir));

			return outputs;
		}
		catch (const std::exception&)
		{
			// A document from a newer branch, or one caught mid-write. The caller shows the source
			// with nothing resolved, which is what it would show for an unimported `.glb` anyway.
			return {};
		}
	}

	QStringList
	ImportTexturesOf(const QString& dataRoot, const QString& source)
	{
		const QString directory = ImportOutputsOf(dataRoot, source).textureDir;
		if (directory.isEmpty())
			return {};

		auto found = QStringList();
		for (const QFileInfo& entry : QDir(directory).entryInfoList(
				 { QStringLiteral("*") + Suffix(assetlib::c_TextureExtension) },
				 QDir::Files,
				 QDir::Name))
			found.push_back(entry.absoluteFilePath());

		return found;
	}

	void
	ImportOutputsCache::SetDataRoot(const QString& dataRoot)
	{
		m_DataRoot = dataRoot;
		m_Entries.clear();
	}

	ImportOutputs
	ImportOutputsCache::Of(const QString& source) const
	{
		const QString document = DocumentFor(m_DataRoot, source);
		if (document.isEmpty())
			return {};

		// Stamped on the document and not on the source: the `.glb` does not change when a reimport
		// writes different containers, and the document is what names them.
		const qint64 stamp = FileStamp(document);

		const auto cached = m_Entries.constFind(source);
		if (cached != m_Entries.cend() && cached->stamp == stamp)
			return cached->outputs;

		const Entry entry = { ImportOutputsOf(m_DataRoot, source), stamp };
		m_Entries.insert(source, entry);
		return entry.outputs;
	}
}
