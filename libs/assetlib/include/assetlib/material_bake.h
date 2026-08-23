#pragma once

namespace assetlib
{
	struct BMaterial;

	[[nodiscard]] bool
	isBakedMapName(std::string_view fileName) noexcept;

	/**
	 * Strips the authoring data from a baked material, leaving the shippable form: the triplet, the
	 * factors and the name. Clears `routes`, `routeStamps` and `editorGraph`, so the triplet is all
	 * that is left to draw from and nothing can report the bake stale.
	 *
	 * @throws std::runtime_error if `material` has not been baked (it has routes but no baked maps) --
	 *         stripping then would destroy the only description of the material.
	 */
	void
	stripAuthoringData(BMaterial& material);
}
