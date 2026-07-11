#pragma once
#include <assetlib_structs/BMaterial.h>

namespace assetlib
{
	/**
	 * Where a bake reads and writes.
	 *
	 * Every texture path a `.bmaterial` stores -- its routed sources and its baked triplet alike -- is
	 * relative to `dataRoot`, never to the material file. So a material in `Data/Materials/` names its
	 * baked base colour `Textures/basecolor_a1b2c3d4.ktx2` and its source `textures_src/tex1.ktx2`,
	 * whatever directory it happens to live in. A standalone baked model directory is its own data root.
	 */
	struct MaterialBakeDesc
	{
		std::filesystem::path dataRoot;  // the project's Data directory
		std::filesystem::path textureDir =
			"Textures";  // baked maps land here, relative to dataRoot
	};

	/**
	 * Composites a material's per-channel routes down to the optimized baseColor / normal / orm triplet,
	 * writing one `.ktx2` per map into `desc.dataRoot / desc.textureDir` and updating `material` in
	 * place.
	 *
	 * **Baked maps are shared, not owned.**
	 *
	 * @throws std::runtime_error if nothing is routed, a source is missing or undecodable, or a map
	 *         cannot be written.
	 */
	void
	bakeMaterial(BMaterial& material, const MaterialBakeDesc& desc);

	/**
	 * Whether `fileName` is a name bakeMaterial could have written: `<group>_<16 hex digits>.ktx2`.
	 *
	 * The counterpart of the bake's naming, and deliberately kept beside it so the two cannot drift. It
	 * is what lets a prune tell a baked map apart from a hand-placed one sharing the directory
	 * (`skybox.ktx2`, `brdf_lut.ktx2`), which must never be swept. Matching the pattern says the bake
	 * *could* have written the file, not that it did, and never that anything still references it.
	 */
	[[nodiscard]] bool
	isBakedMapName(std::string_view fileName) noexcept;

	/**
	 * Strips the authoring data from a baked material, leaving the shippable form: the triplet, the
	 * factors and the name. Clears `routes`, `routeStamps` and `editorGraph`, and forces kBaked.
	 *
	 * @throws std::runtime_error if `material` has not been baked (it has routes but no baked maps) --
	 *         stripping then would destroy the only description of the material.
	 */
	void
	stripAuthoringData(BMaterial& material);
}
