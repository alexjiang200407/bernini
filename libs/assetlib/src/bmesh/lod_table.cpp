#include "bmesh/lod_table.h"
#include <algorithm>
#include <assetlib_structs/Mesh.h>
#include <core/err/util.h>
#include <core/str/string_pool.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assetlib
{
	float
	defaultLodMinPixels(const uint32_t level, const uint32_t lodCount) noexcept
	{
		if (level + 1u >= lodCount)
			return 0.0f;
		return 160.0f / static_cast<float>(1u << level);
	}

	void
	writeLodTables(
		const std::span<Mesh>        meshes,
		std::vector<MeshLod>&        lods,
		const std::span<const float> authored,
		const core::string_pool&     names)
	{
		lods.clear();

		const bool anyLevels =
			std::ranges::any_of(meshes, [](const Mesh& mesh) { return mesh.lodCount > 1; });
		if (!anyLevels && authored.empty())
		{
			for (Mesh& mesh : meshes) mesh.firstLod = 0;
			return;
		}

		for (Mesh& mesh : meshes)
		{
			if (authored.size() > mesh.lodCount)
			{
				core::throw_runtime_error(
					"lodMinPixels lists {} levels, but mesh '{}' carries {}; list no more levels "
					"than the source's meshes have",
					authored.size(),
					names.at(mesh.nameOffset),
					mesh.lodCount);
			}

			mesh.firstLod = static_cast<uint32_t>(lods.size());
			for (uint32_t level = 0; level < mesh.lodCount; ++level)
			{
				const float minPixels = level < authored.size() ?
				                            authored[level] :
				                            defaultLodMinPixels(level, mesh.lodCount);
				if (level > 0 && minPixels > lods.back().minPixels)
				{
					core::throw_runtime_error(
						"mesh '{}': level {} would be drawn from {} pixels but level {} only from "
						"{}; the authored lodMinPixels and the defaults after them rise, so author "
						"every level",
						names.at(mesh.nameOffset),
						level,
						minPixels,
						level - 1,
						lods.back().minPixels);
				}
				lods.push_back({ minPixels });
			}
		}
	}
}
