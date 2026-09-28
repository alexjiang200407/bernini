#include "lod_view.h"

#include <assetlib/bmesh.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <bgl/glm.h>
#include <bgl/lod_select.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace editor
{
	MeshLods
	LodsOf(const assetlib::BMesh& mesh, const uint32_t meshIndex)
	{
		auto lods      = MeshLods();
		lods.minPixels = assetlib::meshLodMinPixels(mesh, meshIndex);

		const auto coarsest = static_cast<uint32_t>(lods.minPixels.size());
		if (coarsest == 0 || assetlib::meshLodSubmeshes(mesh, meshIndex, coarsest - 1).empty())
			return {};

		const std::span<const assetlib::Submesh> levelZero =
			assetlib::meshLodSubmeshes(mesh, meshIndex, 0);
		if (levelZero.empty())
			return {};

		auto minBound = glm::vec3(std::numeric_limits<float>::max());
		auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
		for (const assetlib::Submesh& submesh : levelZero)
		{
			minBound = glm::min(minBound, submesh.aabbMin);
			maxBound = glm::max(maxBound, submesh.aabbMax);
		}
		lods.levelZeroSphere = bgl::BoundingSphereOf(minBound, maxBound);
		return lods;
	}

	LodReadout
	ReadLod(
		const MeshLods&               lods,
		const glm::mat4&              world,
		const glm::vec3&              eye,
		const float                   pixelsPerUnit,
		const float                   pixelScale,
		const std::optional<uint32_t> forced,
		const std::optional<uint32_t> previous) noexcept
	{
		auto readout   = LodReadout();
		readout.pixels = bgl::ProjectedDiameter(
			bgl::TransformSphere(world, lods.levelZeroSphere),
			eye,
			pixelsPerUnit);

		const auto count = static_cast<uint32_t>(lods.minPixels.size());
		if (forced.has_value() && count > 0)
			readout.level = std::min(*forced, count - 1);
		else
			readout.level = bgl::ChooseLevel(lods.minPixels, readout.pixels, pixelScale, previous);
		return readout;
	}
}
