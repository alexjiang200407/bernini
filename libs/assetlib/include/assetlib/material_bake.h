#pragma once

#include <string_view>
namespace assetlib
{
	struct BMaterial;

	[[nodiscard]] bool
	isBakedMapName(std::string_view fileName) noexcept;

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
