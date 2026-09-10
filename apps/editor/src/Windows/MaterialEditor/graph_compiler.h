#pragma once

#include "Windows/MaterialEditor/MaterialGraphSet.h"

#include <assetlib_structs/BMaterial.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <filesystem>
#include <functional>
#include <string>

class MaterialPreviewWindow;
class Renderer;

namespace editor
{
	/** Hands back the handle to bind for a texture named by its data-root-relative key. */
	using TextureLoader = std::function<bgl::TextureAssetHandle(const std::string&)>;

	/**
	 * The surface half of `material` as the renderer takes it: the surface it names, the layer keys,
	 * and every value and texture it sets by name.
	 *
	 * `loadTexture` is handed each bound texture's key, data-root-relative, and returns the handle to
	 * bind -- null for one it could not load, which leaves the surface sampling its default rather
	 * than losing the whole material to one bad path.
	 *
	 * Only the values the document names are sent. What it leaves out takes the default the surface
	 * declared, which is the renderer's rule and not this function's to anticipate.
	 */
	[[nodiscard]] bgl::SurfaceMaterialDesc
	SurfaceDescOf(const assetlib::BMaterial& material, const TextureLoader& loadTexture);

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
	 * A surface material is drawn by its own surface rather than by the board: `onDisk` is what the
	 * document says, and a graphless board would otherwise compile to the PBR defaults it was seeded
	 * with. Its bound textures are read from under `dataRoot`, which is what their keys are relative
	 * to. Pass `onDisk` null for a material that has never been written.
	 *
	 * Does nothing for a graph with no sink.
	 */
	void
	CompilePreviewMaterial(
		MaterialGraphSet::Graph&     graph,
		Renderer&                    renderer,
		MaterialPreviewWindow&       preview,
		const assetlib::BMaterial*   onDisk,
		const std::filesystem::path& dataRoot);
}
