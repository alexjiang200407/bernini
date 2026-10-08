#include "util/GrassField.h"
#include <algorithm>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <core/glm.h>
#include <cstdint>

namespace bgl::test
{
	assetlib::BGrassFields
	MakeGrassField(const uint32_t side, const float spacing)
	{
		auto grass  = assetlib::BGrassFields();
		grass.looks = { "unused.bgrass" };
		grass.names = { "Ground" };

		const float origin = -0.5f * spacing * static_cast<float>(side - 1);
		for (uint32_t y = 0; y < side; ++y)
		{
			for (uint32_t x = 0; x < side; ++x)
			{
				grass.clumps.push_back(
					assetlib::GrassClump{ .position = glm::vec3(
											  origin + spacing * static_cast<float>(x),
											  origin + spacing * static_cast<float>(y),
											  0.0f),
				                          .heightScale = 1.0f,
				                          .normal      = glm::vec3(0.0f, 0.0f, 1.0f),
				                          .color       = glm::u8vec4(255) });
			}
		}

		auto field = assetlib::GrassField{ .mesh = 0, .look = 0, .firstChunk = 0, .chunkCount = 0 };
		for (uint32_t first = 0; first < grass.clumps.size();
		     first += assetlib::c_GrassClumpsPerChunk)
		{
			const auto count = std::min<uint32_t>(
				assetlib::c_GrassClumpsPerChunk,
				static_cast<uint32_t>(grass.clumps.size()) - first);

			glm::vec3 lo(1e30f);
			glm::vec3 hi(-1e30f);
			for (uint32_t k = first; k < first + count; ++k)
			{
				lo = glm::min(lo, grass.clumps[k].position);
				hi = glm::max(hi, grass.clumps[k].position);
			}

			grass.chunks.push_back(
				assetlib::GrassChunk{ .boundingCenter = (lo + hi) * 0.5f,
			                          .boundingRadius = glm::distance(lo, hi) * 0.5f,
			                          .firstClump     = first,
			                          .clumpCount     = count,
			                          .maxHeightScale = 1.0f });
			++field.chunkCount;
		}
		grass.fields = { field };
		return grass;
	}
}
