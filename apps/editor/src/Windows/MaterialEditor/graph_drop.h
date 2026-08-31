#pragma once

#include <QString>

class QMimeData;

namespace editor
{
	/** What a drag is offering the material graph canvas. */
	struct GraphDrop
	{
		/** A texture to make a node from, absolute. */
		QString texture;

		/**
		 * An imported source whose extracted textures are to be offered, absolute. Empty when the
		 * payload named a texture directly.
		 */
		QString source;
	};

	/**
	 * What a drag is offering the canvas: a `.ktx2` to make a node from, or an imported source to
	 * offer the textures of. Both empty when it carries neither, which is the canvas letting the
	 * drag through to the base view beneath it.
	 *
	 * By extension alone -- nothing is opened, because this answers every `dragMoveEvent`, and the
	 * view has no project to resolve a source against in any case. Which textures a source has is
	 * the window's question, and it is asked once, at the drop.
	 */
	[[nodiscard]] GraphDrop
	GraphDroppedOn(const QMimeData* mime);

	/** Whether the canvas takes this drag at all. The accept filter. */
	[[nodiscard]] bool
	IsGraphDrag(const QMimeData* mime);
}
