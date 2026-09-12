#pragma once

#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"

#include <bgl/types/SurfaceMaterialDesc.h>
#include <cstddef>
#include <filesystem>
#include <qstring.h>

class MaterialPreviewWindow;
class Renderer;
class SlotComposer;

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

	/** The cache key naming `slot`'s current route set: the compile requests a compose under it,
	 *  and a delivery applies only while the board still answers with the same key. */
	[[nodiscard]] QString
	SlotRouteKey(const SurfaceOutputNode& sink, size_t slot);

	/**
	 * The composed-slot cache entry a delivered compose should fill: `slot`'s still-empty entry
	 * under `key`, provided the board still routes exactly the routes that were composed. Null
	 * when the board moved on -- rewired, rebuilt, no longer a surface board, or already
	 * delivered -- and the caller then drops the image; the current routes have a compose of
	 * their own in flight.
	 */
	[[nodiscard]] MaterialGraphSet::Graph::ComposedSlot*
	PendingComposedSlot(MaterialGraphSet::Graph& graph, size_t slot, const QString& key);

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
	 * edited value reaches the viewport per keystroke exactly as a PBR factor does. A *routed*
	 * data slot is composited once per route set (ADR-8's editor half, cached on the graph) --
	 * `dataRoot` is what the sources resolve against, and with none the slot samples the default
	 * map. The compose runs on `composer`'s worker rather than here: the slot samples the default
	 * map until the delivery -- matched back through PendingComposedSlot under `graphIndex` --
	 * uploads it and recompiles.
	 *
	 * Does nothing for a graph with no sink.
	 */
	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph&     graph,
		int                          graphIndex,
		Renderer&                    renderer,
		MaterialPreviewWindow&       preview,
		SlotComposer&                composer,
		const std::filesystem::path& dataRoot);
}
