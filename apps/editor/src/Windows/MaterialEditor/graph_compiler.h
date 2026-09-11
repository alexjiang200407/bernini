#pragma once

#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"

#include <bgl/types/SurfaceMaterialDesc.h>

class MaterialPreviewWindow;
class Renderer;

namespace editor
{
	/**
	 * The surface half of a live board as the renderer takes it: the surface the sink reflects,
	 * its layer keys, every declared value at its current setting, and each bound slot with the
	 * handle its Texture node uploaded. A bound texture that could not upload stays bound with a
	 * null handle -- the surface samples its default for it, a visible mistake rather than a lost
	 * material -- and an unbound slot is simply absent.
	 */
	[[nodiscard]] bgl::SurfaceMaterialDesc
	SurfaceDescOfBoard(const SurfaceOutputNode& sink);

	/**
	 * Compiles `graph`'s sink into the material the preview draws it through, and binds every
	 * submesh the graph drives to it.
	 *
	 * Called on every keystroke, so it rewrites the existing handle in place where it can: the
	 * instances already overriding with it then follow the edit with no rebinding. Only while the PSO
	 * bucket is unchanged, which the layer decides and an update cannot rewrite -- so changing the
	 * alpha mode does mean a new material, and the old one is destroyed only after its replacement is
	 * bound.
	 *
	 * A surface board is drawn by its surface: the desc above, compiled from the live sink, so an
	 * edited value reaches the viewport per keystroke exactly as a PBR factor does.
	 *
	 * Does nothing for a graph with no sink.
	 */
	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph& graph,
		Renderer&                renderer,
		MaterialPreviewWindow&   preview);
}
