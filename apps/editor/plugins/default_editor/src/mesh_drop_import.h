#pragma once

#include <QString>

class QMimeData;

namespace editor
{
	class IEditorHost;

	/**
	 * The `.bmesh` a drag is asking for, absolute, importing the dropped source first when this
	 * project has never imported it.
	 *
	 * What a mesh viewport does with a drop, in one place: a `.bmesh` is taken as itself, a source
	 * the project knows resolves through its import document, and one it does not know is imported
	 * -- the host's own glTF cook, dialog and loading screen (IEditorHost::ImportMeshSource).
	 *
	 * @return Empty when the drag carried no mesh at all, and when the import produced none:
	 *         declined, cancelled, refused, or a source imported for its clips. The host reports
	 *         whichever of those it was, so an empty answer is not the caller's to explain.
	 */
	[[nodiscard]] QString
	MeshForDrop(IEditorHost& host, const QMimeData* mime, const QString& dataRoot);
}
