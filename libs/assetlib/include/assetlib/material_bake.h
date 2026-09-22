#pragma once

#include <string>
#include <string_view>
namespace assetlib
{
	struct BMaterial;

	[[nodiscard]] bool
	isBakedMapName(std::string_view fileName) noexcept;

	/**
	 * The mount key a material's texture reference is drawn from. A reference ending `.ktx2` names a
	 * file and comes back as it is; an empty one stays empty. Anything else is a baked map's content
	 * name, `<dir>/<group>_<16 hex>`, which names no file: it resolves to the one the encoding table
	 * stores that group's role in today.
	 *
	 * @throws std::runtime_error if a content name's group is not one a material bake writes.
	 */
	[[nodiscard]] std::string
	bakedTextureKey(std::string_view reference);

	/**
	 * Strips the authoring data from a baked material, leaving the shippable form: the baked maps,
	 * the factors and the name. Clears `routes`, `routeStamps`, the authored occlusion map and its
	 * stamp, and `editorGraph`, so the baked maps are all that is left to draw from and nothing can
	 * report the bake stale.
	 *
	 * @throws std::runtime_error if `material` has not been baked (it has routes but no baked maps,
	 *         or an authored occlusion map and no baked one) -- stripping then would destroy the
	 *         only description of the material.
	 */
	void
	stripAuthoringData(BMaterial& material);
}
