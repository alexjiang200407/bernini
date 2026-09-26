#include "grass_chunks.h"

#include <algorithm>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Node.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_MortonBitsPerAxis = 10;

		/** Spreads the low ten bits of `v` so two zero bits sit between each. */
		[[nodiscard]] uint32_t
		spreadBits(uint32_t v) noexcept
		{
			v &= 0x3ffu;
			v = (v | (v << 16u)) & 0x030000ffu;
			v = (v | (v << 8u)) & 0x0300f00fu;
			v = (v | (v << 4u)) & 0x030c30c3u;
			v = (v | (v << 2u)) & 0x09249249u;
			return v;
		}

		[[nodiscard]] uint32_t
		mortonCode(const glm::vec3& unit) noexcept
		{
			constexpr float  c_Scale = static_cast<float>((1u << c_MortonBitsPerAxis) - 1u);
			const glm::uvec3 cell    = glm::uvec3(glm::clamp(unit, 0.0f, 1.0f) * c_Scale);
			return spreadBits(cell.x) | (spreadBits(cell.y) << 1u) | (spreadBits(cell.z) << 2u);
		}
	}

	void
	appendGrassField(
		BGrassFields&           grass,
		std::vector<GrassClump> clumps,
		const uint32_t          mesh,
		std::string             name)
	{
		glm::vec3 lo(std::numeric_limits<float>::max());
		glm::vec3 hi(std::numeric_limits<float>::lowest());
		for (const GrassClump& clump : clumps)
		{
			lo = glm::min(lo, clump.position);
			hi = glm::max(hi, clump.position);
		}

		// A flat field has no extent on one axis; any positive divisor puts it at cell zero.
		const glm::vec3 extent = glm::max(hi - lo, glm::vec3(std::numeric_limits<float>::min()));

		auto keyed = std::vector<std::pair<uint32_t, uint32_t>>();
		keyed.reserve(clumps.size());
		for (uint32_t i = 0; i < clumps.size(); ++i)
			keyed.emplace_back(mortonCode((clumps[i].position - lo) / extent), i);

		// By code, then by source index, so two clumps in one cell keep the order they came in.
		std::ranges::sort(keyed);

		auto field       = GrassField();
		field.mesh       = mesh;
		field.look       = c_InvalidIndex;
		field.firstChunk = static_cast<uint32_t>(grass.chunks.size());

		for (size_t first = 0; first < keyed.size(); first += c_GrassClumpsPerChunk)
		{
			const size_t count = std::min<size_t>(c_GrassClumpsPerChunk, keyed.size() - first);

			glm::vec3 chunkLo(std::numeric_limits<float>::max());
			glm::vec3 chunkHi(std::numeric_limits<float>::lowest());
			float     maxHeightScale = 0.0f;
			for (size_t k = first; k < first + count; ++k)
			{
				const GrassClump& clump = clumps[keyed[k].second];
				chunkLo                 = glm::min(chunkLo, clump.position);
				chunkHi                 = glm::max(chunkHi, clump.position);
				maxHeightScale          = std::max(maxHeightScale, clump.heightScale);
			}

			auto chunk           = GrassChunk();
			chunk.boundingCenter = (chunkLo + chunkHi) * 0.5f;
			chunk.boundingRadius = 0.0f;
			chunk.firstClump     = static_cast<uint32_t>(grass.clumps.size());
			chunk.clumpCount     = static_cast<uint32_t>(count);
			chunk.maxHeightScale = maxHeightScale;

			for (size_t k = first; k < first + count; ++k)
			{
				const GrassClump& clump = clumps[keyed[k].second];
				chunk.boundingRadius    = std::max(
					chunk.boundingRadius,
					glm::distance(chunk.boundingCenter, clump.position));
				grass.clumps.emplace_back(clump);
			}

			grass.chunks.emplace_back(chunk);
			++field.chunkCount;
		}

		grass.fields.emplace_back(field);
		grass.names.emplace_back(std::move(name));
	}
}
