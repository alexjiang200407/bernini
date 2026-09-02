#pragma once
#include <core/glm.h>

namespace assetlib
{
	/**
	 * A rig, the pose it is put into, and the vertices that pose moves.
	 *
	 * One header because it is one pipeline: `poseModelTransforms` walks a clip frame into
	 * model space, `skinningMatrices` composes each with its inverse bind, and `skinSubmesh`
	 * blends four influences per vertex against exactly those matrices. Split across two, the
	 * producer and its consumer sat on opposite sides of the cut.
	 *
	 * Deliberately the unoptimised form: this is the reference every GPU path is diffed
	 * against. See docs/skinning.md.
	 */

	struct AnimationSet;
	struct BMesh;
	struct Bounds;
	struct ClipFloor;
	struct Skeleton;
	struct Submesh;

	/**
	 * A hash over every bone's name and parent -- everything a joint index means.
	 *
	 * A clip set and a skinned mesh both address a skeleton by bare index, and nothing about a wrong
	 * index is detectable from the pose it produces. So each records the signature of the rig it was
	 * built against, and a re-export that inserts or reorders a bone is caught when the two are
	 * brought together rather than by a limb pointing the wrong way.
	 *
	 * Deliberately not a hash of the bind pose: re-authoring the rest pose of a rig does not
	 * invalidate a clip, and treating it as though it did would make every skeleton edit a re-cook of
	 * every clip set.
	 */
	[[nodiscard]] uint64_t
	skeletonSignature(const Skeleton& skeleton) noexcept;

	/**
	 * @throws std::runtime_error if the bones are not topologically sorted (a parent at or after its
	 *         child), a parent index is out of range, or a name offset is past the string pool.
	 */
	void
	validateSkeleton(const Skeleton& skeleton);

	/**
	 * @throws std::runtime_error if a clip's sample range falls outside `samples`, its frame count is
	 *         zero, or `boneCount` disagrees with the sample pool.
	 */
	void
	validateAnimationSet(const AnimationSet& animations);

	/** The index of the bone named `name`, or nullopt. Linear: only tooling asks. */
	[[nodiscard]] std::optional<uint32_t>
	findBone(const Skeleton& skeleton, std::string_view name);

	/**
	 * Each bone's model-space bind transform, in bone order. One forward pass, because the bones are
	 * topologically sorted.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	bindPoseModelTransforms(const Skeleton& skeleton);

	/**
	 * Each bone's model-space transform at `frame` of `clip`, in bone order.
	 *
	 * The samples are local to each bone's parent, so this is the same forward pass
	 * bindPoseModelTransforms makes, over a clip's pose instead of the rest one.
	 *
	 * @throws std::runtime_error if `clip` or `frame` is out of range, if `animations` was cooked
	 *         against a different rig (by animationsMatchSkeleton), or if the clip's samples fall
	 *         outside the pool.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	poseModelTransforms(
		const Skeleton&     skeleton,
		const AnimationSet& animations,
		uint32_t            clip,
		uint32_t            frame);

	/**
	 * One weighted contribution to a blended pose: a clip, the fractional frame of it to sample,
	 * and the weight it carries.
	 *
	 * `frames` is clip-relative and already wrapped or clamped -- the clock is the caller's, as it
	 * is the GPU's `ClipFrames` -- and a pose between two frames is their nlerp with a hemisphere
	 * flip, which is what `pose_walk.slang` does.
	 */
	struct BlendSample
	{
		uint32_t clip;
		float    frames;
		float    weight;
	};

	/**
	 * Each bone's model-space transform of the blend of `blend`'s samples, in bone order.
	 *
	 * Blended local to each bone's parent, then walked once: translation and scale by the weighted
	 * sum, rotation by accumulating the samples in order with each quaternion flipped into the
	 * hemisphere of the sum so far, then normalized. The weights are normalized to one. This is the
	 * reference a crossfade or a blend space on the GPU is diffed against.
	 *
	 * @throws std::runtime_error if `blend` is empty, a weight is negative, infinite or not a number
	 *         or they sum to zero, a clip is out of range or holds no frames, `frames` is outside
	 *         `[0, frameCount - 1]`, or `animations` was cooked against a different rig.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	poseModelTransforms(
		const Skeleton&              skeleton,
		const AnimationSet&          animations,
		std::span<const BlendSample> blend);

	/**
	 * The matrix each bone skins a vertex by: its model-space pose times its inverse bind.
	 *
	 * The inverse bind is what takes a vertex from model space into the bone's own space, so the
	 * product is identity for a pose that equals the bind pose -- which is why a rest-pose frame
	 * reproduces the source mesh exactly rather than approximately.
	 *
	 * @throws std::runtime_error if `modelTransforms` is not one per bone.
	 */
	[[nodiscard]] std::vector<glm::mat4>
	skinningMatrices(const Skeleton& skeleton, std::span<const glm::mat4> modelTransforms);

	/**
	 * The skeleton path `path` names, read without its samples: the header, the chunk table and the
	 * reference chunk alone. A whole-project reference scan comes through here -- the samples are
	 * megabytes and the path is a few dozen bytes.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] std::string
	loadAnimationSkeletonPath(const std::filesystem::path& path);

	/**
	 * Whether `animations` was resampled against `skeleton`, by signature. A mismatch means the rig
	 * has had a bone inserted, removed or reordered since the clips were cooked, and their joint
	 * indices now name different bones.
	 */
	[[nodiscard]] bool
	animationsMatchSkeleton(const AnimationSet& animations, const Skeleton& skeleton) noexcept;

	/**
	 * Whether `mesh` was cooked against `skeleton`, by signature. A mismatch means the rig has had a
	 * bone inserted, removed or reordered since -- or that the two were never a pair -- and the
	 * mesh's joint indices now name different bones.
	 *
	 * A static mesh matches any rig: it carries no joint indices, so there is nothing to misname.
	 */
	[[nodiscard]] bool
	meshMatchesSkeleton(const BMesh& mesh, const Skeleton& skeleton) noexcept;

	/**
	 * A box holding mesh `meshIndex` in every pose of every clip, bounded one bone at a time: each
	 * bone carries a box over the vertices it has weight on, in its own frame, and a pose sweeps
	 * that box rather than the vertices inside it.
	 *
	 * A bind-pose box is not a substitute: a pose reaches outside it the moment a limb extends, and a
	 * clip carrying root motion walks the whole rig out of it.
	 *
	 * Conservative, never tight: a skinned position is a convex combination of its bones' products,
	 * so it lies inside the bounding box of their union -- which is what accumulating a min and a
	 * max builds. An axis-aligned box swept by a rotation gains slack the vertices themselves do
	 * not. exactPosedBounds is what that slack is measured against -- see
	 * docs/skinning.md. Cheap enough to measure at load: it costs a box per bone per frame instead
	 * of a vertex, which on a 663-bone rig with 170k vertices is 1.5 M products rather than 383 M.
	 *
	 * The convexity holds only while the weights sum to one, and quantized ones sum to within four
	 * unorm16 roundings of it, so a vertex may fall outside by that fraction of its distance from
	 * the bone -- ~3e-5, and below the precision anything culls at.
	 *
	 * @throws std::runtime_error if `meshIndex` is out of range, or for anything poseModelTransforms
	 *         or decodeInfluences refuses (a clip set cooked against another rig, a bad joint index).
	 */
	[[nodiscard]] Bounds
	posedBounds(
		const BMesh&        mesh,
		uint32_t            meshIndex,
		const Skeleton&     skeleton,
		const AnimationSet& animations);

	/**
	 * The tightest such box, by skinning every vertex at every frame -- the ground truth posedBounds
	 * is diffed against, the way skinSubmesh is the reference a GPU skin is diffed against.
	 *
	 * Nothing in the pipeline calls this: it costs a vertex per frame per clip, which is minutes on
	 * an AAA rig. Reach for it to measure how loose a posed box is, not to bake one.
	 *
	 * @throws std::runtime_error for everything posedBounds does.
	 */
	[[nodiscard]] Bounds
	exactPosedBounds(
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
	 * Every entry shares one walk of the clip set, so a rig drawn as many meshes evaluates each pose
	 * once rather than once per mesh.
	 *
	 * A mesh entry the walk refuses gets no box rather than failing the cook: the box is derived
	 * data, and a load measures -- and reports -- exactly as it would had this never run. A clip set
	 * cooked against another rig refuses every entry, since the walk they share is what refuses.
	 */
	void
	bakePosedBounds(AnimationSet& animations, const BMesh& mesh, const Skeleton& skeleton);

	/**
	 * The boxes bakePosedBounds stored for `mesh` against this clip set, one per entry of
	 * `mesh.meshes`. An entry's box is nullopt where the `.banim` carries none -- never baked, a
	 * different mesh, or a source that changed since; the caller measures those, and a box that is
	 * there is trusted verbatim.
	 *
	 * Answers for every entry at once because the signature it matches on describes the whole mesh:
	 * asked per entry, it would hash the vertex pool once per entry, which on a 27-entry rig is most
	 * of a second.
	 */
	[[nodiscard]] std::vector<std::optional<Bounds>>
	findPosedBounds(const AnimationSet& animations, const BMesh& mesh, const Skeleton& skeleton);

	/**
	 * The height each clip of `animations` was authored above `y = 0`: the lowest `y` any skinned
	 * vertex of any of `meshes` reaches over the clip, one entry per clip in clip order. A clip
	 * authored on the floor measures 0; the animal rigs measure anywhere from -0.57 to +0.92.
	 *
	 * Several meshes because a clip set is not the property of one: a rig drawn as a body and a
	 * separately imported cloak stands on whichever hangs lower, and a clips-only import has no mesh
	 * of its own at all -- it is grounded against the ones already in the project that skin to its
	 * rig.
	 *
	 * A clip no mesh here has weight on measures 0 -- there is no pose to place it against, and
	 * leaving it where it was authored is the only answer that does not invent one.
	 *
	 * Exact, and paid for by the boxes posedBounds already builds. A bone's box holds every vertex
	 * weighted to it, and a skinned position is a convex combination of its bones' products, so the
	 * lowest box corner is a lower bound on the lowest vertex. That bound is applied twice: once per
	 * frame, to order them and drop every frame already bounded at or above the best floor found,
	 * and once per vertex, so a frame that survives skins only the vertices whose own bones reach
	 * below it. The second is what makes the first affordable on a dense rig -- a box is 1.09-1.51x
	 * loose, so a character standing still leaves roughly a quarter of its frames unpruned, and
	 * skinning all 170k vertices of each is where the walk used to go.
	 *
	 * @throws std::runtime_error for anything posedBounds refuses (a clip set cooked against
	 *         another rig, a malformed vertex layout, a bad joint index).
	 */
	[[nodiscard]] std::vector<float>
	measureClipFloors(
		const AnimationSet&    animations,
		std::span<const BMesh> meshes,
		const Skeleton&        skeleton);

	/**
	 * Move each clip of `animations` down by the floor measureClipFloors reports, so the lowest `y`
	 * `meshes` reach over it rests on 0, recording the move in `AnimationClip::groundOffset`.
	 *
	 * A clip named by `authored` is moved by that floor instead of the measured one -- the escape
	 * hatch for a clip whose lowest frame is not the one standing on the ground. The Coyote's `Land`
	 * is the worked example: its lowest frame is the impact compression, so measuring alone leaves
	 * its settled stance 0.10 above the floor. A name matching no clip is ignored.
	 *
	 * Every root bone is moved, not just bone 0: a root's local translation is already in model
	 * space, so subtracting from it shifts that whole subtree by exactly the amount asked for.
	 * `rootMotion` is a delta and `locomotionSpeed` is horizontal, so neither changes.
	 *
	 * Idempotent -- a grounded clip measures a floor of 0 and is left alone -- so a cook that runs
	 * over an already-grounded `.banim` does not sink it further.
	 *
	 * A clip set the measurement refuses is left ungrounded rather than failing the cook -- whatever
	 * refused it has already been reported where it was read. `authored` still applies: an explicit
	 * floor needs no pose to measure, so one clip the measurement cannot reach does not cost the
	 * rest of the file its overrides.
	 */
	void
	groundClips(
		AnimationSet&              animations,
		std::span<const BMesh>     meshes,
		const Skeleton&            skeleton,
		std::span<const ClipFloor> authored = {});

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
