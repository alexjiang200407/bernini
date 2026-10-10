#pragma once

#include <bgl/glm.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace assetlib
{
	struct BMesh;
}
namespace bgl
{
	class Camera;
}

namespace editor
{
	class IEditorViewport;

	/** What the Mesh Editor reads of one mesh's levels of detail. */
	struct MeshLods
	{
		// Each level's size on screen from which it is drawn, level 0 first.
		std::vector<float> minPixels;

		// The sphere the renderer measures every level by: level 0's submesh boxes, folded.
		glm::vec4 levelZeroSphere = glm::vec4(0.0f);

		// Whether the mesh draws a baked impostor past its last level, rather than nothing.
		bool impostor = false;
	};

	/** The pin for the tier past every level: the mesh's impostor, or nothing where it has none. */
	inline constexpr uint32_t c_ForceImpostor = std::numeric_limits<uint32_t>::max();

	/** `mesh.meshes[meshIndex]`'s levels, or none for a mesh the container does not hold whole. */
	[[nodiscard]] MeshLods
	LodsOf(const assetlib::BMesh& mesh, uint32_t meshIndex);

	/**
	 * The level a placement draws, and the size on screen it was chosen by. A level equal to the
	 * level count is the draw-nothing tier.
	 */
	struct LodReadout
	{
		uint32_t level  = 0;
		float    pixels = 0.0f;
	};

	/**
	 * What the cull draws of `lods` placed at `world`, seen from `eye`: the level `forced` pins --
	 * a mesh with fewer draws its coarsest, and c_ForceImpostor the tier past the last -- or the
	 * level the size earns against the one it drew last. `pixelsPerUnit` is the render grid's (game::PixelsPerUnit).
	 */
	[[nodiscard]] LodReadout
	ReadLod(
		const MeshLods&         lods,
		const glm::mat4&        world,
		const glm::vec3&        eye,
		float                   pixelsPerUnit,
		float                   pixelScale,
		std::optional<uint32_t> forced,
		std::optional<uint32_t> previous) noexcept;

	/**
	 * ReadLod with the renderer's own inputs: `renderRows` is the viewport's
	 * (IEditorViewport::GetRenderHeight), `camera` and `eye` the preview's. Nothing before the
	 * viewport has a size. `previous` is the level read last, rewritten with this one, since the
	 * cull's hysteresis depends on the level it drew last.
	 */
	[[nodiscard]] std::optional<LodReadout>
	ReadLodInView(
		const MeshLods&          lods,
		const glm::mat4&         world,
		const bgl::Camera&       camera,
		const glm::vec3&         eye,
		uint32_t                 renderRows,
		std::optional<uint32_t>  forced,
		std::optional<uint32_t>& previous);

	/**
	 * Pins every placement of `viewport`'s view to `level` (ISceneView::SetLodSelection) -- with
	 * c_ForceImpostor, past its last level -- or none.
	 */
	void
	PinLod(IEditorViewport& viewport, std::optional<uint32_t> level);
}
