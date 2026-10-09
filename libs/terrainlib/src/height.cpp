#include <algorithm>
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

		const auto maxCell = glm::vec2(
			static_cast<float>(field.samplesX - 1),
			static_cast<float>(field.samplesZ - 1));
		const auto cell = glm::clamp(
			(xz - glm::vec2(origin.x, origin.z)) / field.cellSize,
			glm::vec2(0.0f),
			maxCell);

		const auto x0 = static_cast<uint32_t>(cell.x);
		const auto z0 = static_cast<uint32_t>(cell.y);
		const auto x1 = std::min(x0 + 1, field.samplesX - 1);
		const auto z1 = std::min(z0 + 1, field.samplesZ - 1);
		const auto t  = cell - glm::vec2(static_cast<float>(x0), static_cast<float>(z0));

		const auto at = [&](const uint32_t x, const uint32_t z) {
			return static_cast<float>(field.heights[static_cast<size_t>(z) * field.samplesX + x]) /
			       65535.0f;
		};
		const float share = glm::mix(
			glm::mix(at(x0, z0), at(x1, z0), t.x),
			glm::mix(at(x0, z1), at(x1, z1), t.x),
			t.y);
		return origin.y + field.minHeight + share * field.heightRange;
	}
}
