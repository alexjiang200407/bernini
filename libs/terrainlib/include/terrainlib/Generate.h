#pragma once
#include <assetlib_structs/Heightfield.h>
#include <terrainlib/types/TerrainGenerateDesc.h>

namespace terrain
{
	/**
	 * A heightfield of `desc`'s shape, from fractal gradient noise -- ridged for the mountainous
	 * shape -- over a warped domain, seeded by `desc.seed`, scaled by `desc.relief` and then eroded
	 * as `desc.erosion` says. Deterministic as the desc says, and a
	 * different seed is a different field of the same character. The field's
	 * `minHeight` and `heightRange` are the lowest and the span of what was generated, so its
	 * 16-bit samples use their whole range.
	 *
	 * Cost is linear in the samples, spread over the hardware threads; erosion adds a cost linear
	 * in its droplets and their steps, and in the samples times its thermal iterations.
	 *
	 * @throws std::runtime_error if either sample count is below 2 or above
	 *         c_MaxGenerateSamples, `cellSize` is not finite and positive, `relief` is not
	 *         finite and positive, or a field of `desc.erosion` is outside its range.
	 */
	[[nodiscard]] assetlib::Heightfield
	Generate(const TerrainGenerateDesc& desc);
}
