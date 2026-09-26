#include "grass_chunks.h"
#include <assetlib/grass_patch.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace assetlib
{
	BGrassFields
	makeGrassPatch(const GrassPatchDesc& desc, std::string look)
	{
		core::throw_runtime_error_if(
			!std::isfinite(desc.size) || !std::isfinite(desc.spacing) || desc.size <= 0.0f ||
				desc.spacing <= 0.0f,
			"makeGrassPatch: size {} and spacing {} must be finite and positive",
			desc.size,
			desc.spacing);

		const float side = std::floor(desc.size / desc.spacing);
		core::throw_runtime_error_if(
			side < 1.0f || side * side > 16777216.0f,
			"makeGrassPatch: a {} square at {} spacing holds {} clumps, outside [1, 2^24]",
			desc.size,
			desc.spacing,
			side * side);

		const auto  count  = static_cast<uint32_t>(side);
		const float origin = -0.5f * desc.spacing * static_cast<float>(count - 1);

		std::mt19937                          random(desc.seed);
		std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);
		std::uniform_real_distribution<float> height(0.85f, 1.15f);

		auto clumps = std::vector<GrassClump>();
		clumps.reserve(static_cast<size_t>(count) * count);
		for (uint32_t y = 0; y < count; ++y)
		{
			for (uint32_t x = 0; x < count; ++x)
			{
				const float px = origin + desc.spacing * (static_cast<float>(x) + jitter(random));
				const float py = origin + desc.spacing * (static_cast<float>(y) + jitter(random));
				clumps.push_back(
					GrassClump{ .position    = glm::vec3(px, py, 0.0f),
				                .heightScale = height(random),
				                .normal      = glm::vec3(0.0f, 0.0f, 1.0f),
				                .color       = glm::u8vec4(255) });
			}
		}

		auto grass = BGrassFields();
		appendGrassField(grass, std::move(clumps), 0, "Patch");
		grass.looks          = { std::move(look) };
		grass.fields[0].look = 0;
		return grass;
	}
}
