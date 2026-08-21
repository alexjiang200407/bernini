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
	 * A bind-pose box is not a substitute: a pose reaches outside it the moment a limb extends, and a
	 * clip carrying root motion walks the whole rig out of it.
	 *
	 * Bounding the *bones* instead is tempting and much cheaper, but it is not close enough to use:
	 * applying every bone's matrix to the whole bind-pose box over-estimates by ~3x on a rig at that
	 * scale, because each bone is credited with moving vertices it has no weight on. This is exact
	 * instead, and costs a vertex per frame per clip -- seconds on a rig with a full clip set (1372
	 * frames over 4309 vertices is ~3.5 s in a debug build), so it belongs on a worker -- or,
	 * better, at cook: bakePosedBounds below stores the result in the `.banim` so a load only has
	 * to find it.
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

	/**
	 * What a baked posed box was measured against: the mesh's vertex data, the entry and submesh
	 * tables that address it, and the skeleton's inverse binds -- everything posedBounds reads
	 * that does not live in the `.banim` itself. A re-imported mesh or a re-authored bind changes
	 * it; renames, material swaps and clip edits do not.
	 */
	[[nodiscard]] uint64_t
	posedBoundsSignature(const BMesh& mesh, const Skeleton& skeleton) noexcept;

	/**
	 * Measure posedBounds for every skinned mesh entry of `mesh` and store the boxes on
	 * `animations`, keyed by posedBoundsSignature -- what an import runs before saving the
	 * `.banim`, so no load has to.
	 *
	 * Entries the same signature left behind earlier are replaced; another mesh's are kept, since
	 * one clip set may be cooked beside several meshes.
	 *
	 * A mesh entry the walk refuses gets no box rather than failing the cook: the box is derived
	 * data, and a load measures -- and reports -- exactly as it would had this never run.
	 */
	void
	bakePosedBounds(AnimationSet& animations, const BMesh& mesh, const Skeleton& skeleton);

	/**
	 * The box bakePosedBounds stored for this exact pairing, or nullopt when the `.banim` carries
	 * none -- never baked, a different mesh, or a source that changed since. The caller measures
	 * then; the box itself is trusted verbatim.
	 */
	[[nodiscard]] std::optional<Bounds>
	findPosedBounds(
		const AnimationSet& animations,
		const BMesh&        mesh,
		uint32_t            meshIndex,
		const Skeleton&     skeleton) noexcept;

	/** One vertex after skinning, in model space. */
	struct SkinnedVertex
	{
		glm::vec3 position;

		// Blended, so not unit length: two rotations shorten it and a scaled bone lengthens it.
		// Zero when the submesh carries no normals.
		glm::vec3 blendedNormal;

		// The tangent's xyz, blended as the normal is; its handedness is the bind tangent's `w`,
		// which no rotation changes. Zero when the submesh carries no tangents.
		glm::vec3 blendedTangent;
	};

	/**
	 * Every vertex of `submesh` skinned by `skinning`, in the submesh's own vertex order.
	 *
	 * Linear blend skinning, four influences, deliberately unoptimised -- see libs/assetlib/CLAUDE.md
	 * for why this lives here rather than in bgl.
	 *
	 * Normals and tangents ride the same matrices rather than their inverse transpose. That is an
	 * accepted limitation, not a property of rigs: it is exact only while a bone's scale is
	 * uniform, and nothing rejects the non-uniform scale glTF permits -- a squash-and-stretch rig
	 * imports cleanly and skins normals that are wrong. The GPU path will make the same trade.
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
