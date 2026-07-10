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
	 * place: the triplet paths are filled, `routeStamps` records what each source measured, and `mode`
	 * becomes kBaked. The routes and the editor graph are *kept* -- a baked material can still be
	 * reopened, re-authored and re-baked. Only `stripAuthoringData` removes them.
	 *
	 * The maps are written in their final block formats, so the runtime uploads them without
	 * transcoding: base colour BC1 (sRGB), orm BC7 (unorm), normal BC5 (unorm, X/Y only).
	 *
	 * **Baked maps are shared, not owned.** A map's file name is derived from the content that defines
	 * it -- the group, its resolution, its target format, and the ordered (source, channel) pairs feeding
	 * it. Two materials whose ORM channels route identically therefore name the same file and write it
	 * once, instead of each emitting a byte-identical copy under its own name. A map whose file already
	 * exists and is newer than every source feeding it is not re-encoded.
	 *
	 * Each group is sized independently, to the largest source routed *into that group*. A group's output
	 * thus depends on nothing outside it, which is what lets two materials share one file.
	 *
	 * A routed source may be uncompressed or Basis-supercompressed (what mesh import writes to
	 * `textures_src`); either decodes to RGBA8 texels. It may *not* be an already-block-compressed
	 * `.ktx2` -- a previously baked map -- since BC blocks cannot be decoded back to texels. Channels are
	 * copied as raw bytes: a channel routed into base colour is written into an sRGB map whatever its own
	 * tag said, so keep one decode role per source texture.
	 *
	 * A group with no routed channels is skipped entirely and leaves its triplet path empty; the runtime
	 * substitutes white (base colour, orm) or a flat normal. Base-colour alpha is dropped: BC1_RGB has no
	 * alpha channel.
	 *
	 * @throws std::runtime_error if nothing is routed, a source is missing or undecodable, or a map
	 *         cannot be written.
	 */
	void
	bakeMaterial(BMaterial& material, const MaterialBakeDesc& desc);

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
