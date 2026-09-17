#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

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

	/**
	 * `material`'s shippable form, encoded: stripAuthoringData, then the document. The one write
	 * of a `.bmaterial` without a node graph -- `AssetCodec<BMaterial>::Serialize` refuses that,
	 * since an authored material always carries its graph.
	 *
	 * @throws std::runtime_error as stripAuthoringData.
	 */
	[[nodiscard]] std::vector<std::byte>
	serializeStripped(BMaterial material);
}
