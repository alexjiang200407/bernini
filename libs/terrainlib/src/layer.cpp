#include "bilinear.h"

#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <terrainlib/layer.h>

namespace terrain
{
	float
	LayerAt(const TerrainLayer& layer, const glm::vec3& origin, const glm::vec2 xz) noexcept
	{
		core::ensure(
			layer.samplesX > 0 && layer.samplesZ > 0 &&
				layer.values.size() == static_cast<size_t>(layer.samplesX) * layer.samplesZ,
			"a layer of {} x {} samples holds {}",
			layer.samplesX,
			layer.samplesZ,
			layer.values.size());

		return Bilinear(
			layer.samplesX,
			layer.samplesZ,
			layer.cellSize,
			xz - glm::vec2(origin.x, origin.z),
			[&](const uint32_t x, const uint32_t z) {
				return layer.values[static_cast<size_t>(z) * layer.samplesX + x];
			});
	}
}
