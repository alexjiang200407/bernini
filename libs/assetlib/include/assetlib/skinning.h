#pragma once
#include <core/glm.h>

namespace assetlib
{
	struct AnimationSet;
	struct BMesh;
	struct Bounds;
	struct Skeleton;
	struct Submesh;

	/**
	 * The tightest box holding mesh `meshIndex` in every pose of every clip: every vertex skinned at
	 * every frame, which is the same walk bakeVat makes and the same answer it arrives at.
	 *
	 * A bind-pose box is not a substitute. A rig whose clips are authored in different units than its
	 * bind pose -- a common export -- poses one to two orders of magnitude larger, so a camera framed
	 * or a culling volume sized by the bind pose is wrong by that factor.
	 *
	 * Bounding the *bones* instead is tempting and much cheaper, but it is not close enough to use:
	 * applying every bone's matrix to the whole bind-pose box over-estimates by ~3x on a rig at that
	 * scale, because each bone is credited with moving vertices it has no weight on. Skinning is
	 * milliseconds for a character-sized rig, and it is exact.
	 *
	 * @throws std::runtime_error if `meshIndex` is out of range, or for anything poseModelTransforms
	 *         or skinSubmesh refuses (a clip set cooked against another rig, a bad joint index).
	 */
	[[nodiscard]] Bounds
	posedBounds(
		const BMesh&        mesh,
		uint32_t            meshIndex,
		const Skeleton&     skeleton,
		const AnimationSet& animations);

	/** One vertex after skinning, in model space. */
	struct SkinnedVertex
	{
		glm::vec3 position;

		// Blended, so not unit length: two rotations shorten it and a scaled bone lengthens it.
		// Zero when the submesh carries no normals.
		glm::vec3 blendedNormal;
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
