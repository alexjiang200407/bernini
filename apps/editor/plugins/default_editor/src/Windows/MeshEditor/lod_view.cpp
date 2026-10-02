#include "lod_view.h"

#include <assetlib/bmesh.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/Viewport.h>
#include <core/math.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <gamelib/lod_select.h>

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
		lods.levelZeroSphere = core::bounding_sphere_of(minBound, maxBound);
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
		readout.pixels = game::ProjectedDiameter(
			game::TransformSphere(world, lods.levelZeroSphere),
			eye,
			pixelsPerUnit);

		const auto count = static_cast<uint32_t>(lods.minPixels.size());
		if (forced.has_value() && count > 0)
			readout.level = std::min(*forced, count - 1);
		else
			readout.level = game::ChooseLevel(lods.minPixels, readout.pixels, pixelScale, previous);
		return readout;
	}

	std::optional<LodReadout>
	ReadLodInView(
		const MeshLods&               lods,
		const glm::mat4&              world,
		const bgl::Camera&            camera,
		const glm::vec3&              eye,
		const uint32_t                renderRows,
		const std::optional<uint32_t> forced,
		std::optional<uint32_t>&      previous)
	{
		if (renderRows == 0)
			return std::nullopt;

		const float pixelsPerUnit = game::PixelsPerUnit(
			bgl::Viewport(1.0f, static_cast<float>(renderRows)),
			camera.GetViewProjection());
		const LodReadout readout = ReadLod(
			lods,
			world,
			eye,
			pixelsPerUnit,
			bgl::LodSelectionDesc().pixelScale,
			forced,
			previous);
		previous = readout.level;
		return readout;
	}

	void
	PinLod(IEditorViewport& viewport, const std::optional<uint32_t> level)
	{
		viewport.Invoke([&](RenderContext&, const bgl::SceneViewRef& view) {
			auto selection       = view->GetLodSelection();
			selection.forceLevel = level.has_value() ?
			                           std::optional(static_cast<bgl::LodLevel>(*level)) :
			                           std::nullopt;
			view->SetLodSelection(selection);
		});
	}
}
