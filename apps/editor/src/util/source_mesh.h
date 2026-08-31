#pragma once

#include <QHash>
#include <QString>

namespace editor
{
	/** Whether `path` names an imported source, by its extension alone -- it is never opened. */
	[[nodiscard]] bool
	IsImportedSource(const QString& path);

	/**
	 * The `.bmesh` the source at `path` produced, absolute, or empty. `dataRoot` is the project's
	 * Data directory, which the document's keys are relative to.
	 *
	 * Empty when `path` is not an imported source, when it belongs to another project, when its
	 * `.bimport` is absent or will not parse, or when that document's `outputs` name no mesh. None
	 * of those is an error: a file browser holds paths to files it has never opened, and this
	 * answers a repaint as often as a gesture.
	 *
	 * The mesh is not checked for existence. It is a cache entry, so a source in a fresh checkout
	 * names one no bake has written yet, and a caller that reports the missing file tells the user
	 * more than an empty answer would.
	 */
	[[nodiscard]] QString
	MeshOfSource(const QString& dataRoot, const QString& path);

	/**
	 * MeshOfSource, remembered until the document changes.
	 *
	 * What makes the resolution usable from a model's `data()`, which runs on every paint of every
	 * visible tile: a warm answer costs one `stat`, where re-reading and parsing a document each
	 * time would stall the grid. Staleness is the document's modification time, so -- as with the
	 * pixmap caches -- two writes inside one millisecond share a stamp, and whatever has just
	 * rewritten a document re-roots rather than trusting this to notice.
	 */
	class SourceMeshCache
	{
	public:
		/** The project's Data directory. Forgets what was resolved against the last one. */
		void
		SetDataRoot(const QString& dataRoot);

		[[nodiscard]] QString
		Of(const QString& path) const;

	private:
		struct Entry
		{
			QString mesh;
			qint64  stamp = 0;
		};

		QString                       m_DataRoot;
		mutable QHash<QString, Entry> m_Entries;
	};
}
