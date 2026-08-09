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
		glm::vec3 normal;  // zero when the submesh carries no normals
	};

	/**
	 * Every vertex of `submesh` skinned by `skinning`, in the submesh's own vertex order.
	 *
	 * This is the CPU reference the VAT bake writes and every later GPU path is diffed against, so
	 * it is deliberately the plain form: linear blend skinning, four influences, no optimisation.
	 * Normals are transformed by the same matrices rather than by their inverse transpose --
	 * correct for the rigid and uniformly-scaled bones a rig actually has, and what the GPU path
	 * will do for the same reason.
	 *
	 * A submesh carrying no joints is returned unskinned, which is how a static attachment on a
	 * skinned mesh comes through.
	 *
	 * @throws std::runtime_error if the submesh's vertices fall outside `mesh.vertexData`, if it
	 *         carries joints without weights or the reverse, or if a joint index names no matrix in
	 *         `skinning`.
	 */
	[[nodiscard]] std::vector<SkinnedVertex>
	skinSubmesh(const BMesh& mesh, const Submesh& submesh, std::span<const glm::mat4> skinning);
}
