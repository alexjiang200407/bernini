#include "bilinear.h"

#include <assetlib_structs/Heightfield.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <terrainlib/height.h>

namespace terrain
{
	float
	HeightAt(
		const assetlib::Heightfield& field,
		const glm::vec3&             origin,
		const glm::vec2              xz) noexcept
	{
		core::ensure(
			field.samplesX > 0 && field.samplesZ > 0 &&
				field.heights.size() == static_cast<size_t>(field.samplesX) * field.samplesZ,
			"a heightfield of {} x {} samples holds {}",
			field.samplesX,
			field.samplesZ,
			field.heights.size());

		const float share = Bilinear(
			field.samplesX,
			field.samplesZ,
			field.cellSize,
			xz - glm::vec2(origin.x, origin.z),
			[&](const uint32_t x, const uint32_t z) {
				return static_cast<float>(
						   field.heights[static_cast<size_t>(z) * field.samplesX + x]) /
			           65535.0f;
			});
		return origin.y + field.minHeight + share * field.heightRange;
	}
}
