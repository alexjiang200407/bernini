#pragma once

#include <QString>

class QMimeData;
class QWidget;

namespace editor
{
	/** What a drag is asking a mesh viewport to show. */
	struct MeshDrop
	{
		/** The `.bmesh` to load, absolute, or empty when nothing resolved. */
		QString mesh;

		/**
		 * The imported source `mesh` was resolved from, absolute, or empty when the payload carried
		 * a container directly. Set even where `mesh` is not: a source that named no mesh is a
		 * failure the viewport reports, and this is what it names.
		 */
		QString source;
	};

	/**
	 * Whether a drag carries a file a mesh viewport takes -- a `.bmesh`, or an imported source.
	 * By extension alone: nothing is opened, because this answers every `dragMoveEvent`.
	 *
	 * So it accepts a source that will not resolve; GetMeshDroppedOn is where that is discovered and
	 * ReportUnresolved is what says so. Resolving here instead would read a document from disk on
	 * every mouse move of every drag.
	 */
	[[nodiscard]] bool
	IsMeshDrag(const QMimeData* mime);

	/**
	 * The mesh a drag is asking for: a dropped `.bmesh` as itself, or the mesh the dropped source
	 * produced. Both fields empty when the payload carries neither.
	 *
	 * `dataRoot` resolves a source's document and may be empty, which costs only the source half --
	 * a `.bmesh` is dropped by absolute path and needs no project to read.
	 *
	 * `mesh` is not checked for existence. A `.bmesh` is cache, so a source in a fresh checkout
	 * names one that has not been baked yet, and a load that reports the missing file tells the
	 * user more than this refusing the gesture.
	 */
	[[nodiscard]] MeshDrop
	GetMeshDroppedOn(const QMimeData* mime, const QString& dataRoot);

	/**
	 * Tells the user a dropped source had no mesh to show, in the shape a failed load uses.
	 *
	 * Does nothing when the drop resolved, and nothing when it carried no source: a drag of
	 * something else entirely is not a failure, it is a drop this viewport did not want.
	 */
	void
	ReportUnresolved(QWidget* parent, const MeshDrop& drop);
}
