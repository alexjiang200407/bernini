#pragma once
#include <core/glm.h>

namespace assetlib
{
	struct BMesh;
	struct Submesh;

	/** One vertex after skinning, in model space. */
	struct SkinnedVertex
	{
		glm::vec3 position;

		/**
		 * Zero when the submesh carries no normals, and **not normalized** otherwise: blending two
		 * rotations shortens the result, and a scaled bone lengthens it. A caller that encodes into
		 * a unit-range texel normalizes first.
		 */
		glm::vec3 normal;
	};

	/**
	 * Every vertex of `submesh` skinned by `skinning`, in the submesh's own vertex order.
	 *
	 * Linear blend skinning, four influences, deliberately unoptimised -- see libs/assetlib/CLAUDE.md
	 * for why this lives here rather than in bgl.
	 *
	 * Normals ride the same matrices rather than their inverse transpose. That is an accepted
	 * limitation, not a property of rigs: it is exact only while a bone's scale is uniform, and
	 * nothing rejects the non-uniform scale glTF permits -- a squash-and-stretch rig imports
	 * cleanly and skins normals that are wrong. The GPU path will make the same trade.
	 *
	 * A submesh carrying no joints is returned unskinned, and so is a vertex whose four weights are
	 * all zero -- which is what an exporter writes for a vertex it never assigned to a bone.
	 *
	 * @throws std::runtime_error if the submesh's vertices fall outside `mesh.vertexData`, if an
	 *         attribute this reads is the wrong format or extends past the vertex stride, if the
	 *         submesh carries joints without weights or the reverse, or if a joint index names no
	 *         matrix in `skinning`.
	 */
	[[nodiscard]] std::vector<SkinnedVertex>
	skinSubmesh(const BMesh& mesh, const Submesh& submesh, std::span<const glm::mat4> skinning);
}
