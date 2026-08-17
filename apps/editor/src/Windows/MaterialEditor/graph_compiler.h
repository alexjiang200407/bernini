#pragma once

#include "Windows/MaterialEditor/MaterialGraphSet.h"

class MaterialPreviewWindow;
class Renderer;

namespace editor
{
	/**
	 * Compiles `graph`'s sink into the loose material the preview draws it through, and binds every
	 * submesh the graph drives to it.
	 *
	 * Called on every keystroke, so it rewrites the existing handle in place where it can: the
	 * instances already overriding with it then follow the edit with no rebinding. Only while the PSO
	 * bucket is unchanged, which the layer decides and an update cannot rewrite -- so changing the
	 * alpha mode does mean a new material, and the old one is destroyed only after its replacement is
	 * bound.
	 *
	 * Does nothing for a graph with no sink.
	 */
	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph& graph,
		Renderer&                renderer,
		MaterialPreviewWindow&   preview);
}
