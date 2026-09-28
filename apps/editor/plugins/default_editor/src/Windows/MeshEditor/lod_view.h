#pragma once

#include <bgl/glm.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace assetlib
{
	struct BMesh;
}

namespace editor
{
	/** What the Mesh Editor lists of one mesh's levels of detail, level 0 first. */
	struct MeshLods
	{
		std::vector<uint32_t> triangles;
		std::vector<float>    minPixels;

		// The sphere the renderer measures every level by: level 0's submesh boxes, folded.
		glm::vec4 levelZeroSphere = glm::vec4(0.0f);
	};

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
	 * a mesh with fewer draws its coarsest -- or the level the size earns against the one it drew
	 * last. `pixelsPerUnit` is the render grid's (bgl::PixelsPerUnit).
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
}
