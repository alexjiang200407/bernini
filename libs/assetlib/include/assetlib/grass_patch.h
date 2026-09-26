#pragma once
#include <assetlib_structs/BGrassFields.h>
#include <cstdint>
#include <string>

namespace assetlib
{
	/** A square of ground to grow a grass look on, where no mesh source is: what a preview draws. */
	struct GrassPatchDesc
	{
		float    size    = 20.0f;  // the square's side, in world units
		float    spacing = 0.25f;  // between clumps on the grid they are jittered off
		uint32_t seed    = 1;
	};

	/**
	 * One field of clumps over `desc`'s square, centred on the origin in the XY plane and growing
	 * along +Z -- `IScene::AddPlaneGeom`'s frame, so a plane of the same size is its ground. Each
	 * clump is jittered within its grid cell and its height scale varies a little, both from
	 * `desc.seed` alone, so a patch is the same every run. The field grows on mesh 0, is named
	 * `Patch`, and draws with look slot 0, which names `look`.
	 *
	 * @throws std::runtime_error if `size` or `spacing` is not finite and positive, or the square
	 *         would hold no clump or more than 2^24.
	 */
	[[nodiscard]] BGrassFields
	makeGrassPatch(const GrassPatchDesc& desc, std::string look);
}
