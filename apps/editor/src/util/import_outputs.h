#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace editor
{
	/**
	 * What one imported source produced, as absolute paths. Each is empty where the import wrote
	 * nothing of that kind: a source with no skin produced no rig, and one imported without its
	 * textures has no texture directory.
	 */
	struct ImportOutputs
	{
		QString mesh;
		QString textureDir;
	};

	/** Whether `path` names an imported source, by its extension alone -- it is never opened. */
	[[nodiscard]] bool
	IsImportedSource(const QString& path);

	/**
	 * What the source at `source` produced, read from the `.bimport` beside it. `dataRoot` is the
	 * project's Data directory, which the document's keys are relative to.
	 *
	 * Everything empty when `source` is not an imported source, when its document is absent, or
	 * when the document will not parse. None of those is an error here: a file browser holds paths
	 * to files it has never opened, and this answers a repaint as often as a gesture.
	 */
	[[nodiscard]] ImportOutputs
	ImportOutputsOf(const QString& dataRoot, const QString& source);

	/**
	 * The textures the import at `source` extracted, absolute and sorted, or empty when it wrote
	 * none.
	 *
	 * Read off the directory rather than the document, because a `.ktx2` is keyed by the
	 * `textureDir` and `textureStamp` its `.bimport` carries and never listed in `outputs` -- so
	 * what is in that folder is the only statement of which textures exist.
	 */
	[[nodiscard]] QStringList
	ImportTexturesOf(const QString& dataRoot, const QString& source);

	/**
	 * ImportOutputsOf, remembered until the document changes.
	 *
	 * What makes the resolution usable from a model's `data()`, which runs on every paint of every
	 * visible tile: a warm answer costs one `stat`, where re-reading and parsing a document each
	 * time would stall the grid. Staleness is the document's modification time, so -- as with the
	 * pixmap caches -- two writes inside one millisecond share a stamp, and whatever has just
	 * rewritten a document re-roots rather than trusting this to notice.
	 */
	class ImportOutputsCache
	{
	public:
		/** The project's Data directory. Forgets what was resolved against the last one. */
		void
		SetDataRoot(const QString& dataRoot);

		/** By value: the next call may rehash, and the answer is two shared strings. */
		[[nodiscard]] ImportOutputs
		Of(const QString& source) const;

	private:
		struct Entry
		{
			ImportOutputs outputs;
			qint64        stamp = 0;
		};

		QString                       m_DataRoot;
		mutable QHash<QString, Entry> m_Entries;
	};
}
