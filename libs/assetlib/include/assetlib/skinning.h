#pragma once
#include <core/glm.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
	struct AvatarLegChain;
	struct ResolvedAvatar;
	struct BMesh;
	struct Bounds;
	struct ClipFloor;
	struct PlantWeights;
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
	 * **A rig that authored legs is floored by its soles, not by its lowest vertex.** Given `legs`,
	 * the floor is the lowest a sole plane reaches over the clip -- what the rig stands *on* --
	 * because the lowest vertex need not be a foot: on the test Dog it is not, and its `Idle` soles
	 * then sit 1 and 4 cm above zero. A plant applies the ground's departure from this floor, so a
	 * floor measured somewhere other than the feet is one every planted foot then hovers above.
	 * Cheaper than the vertex walk rather than dearer -- a sole is one transform of one point per
	 * leg per frame, and no vertex is skinned at all.
	 *
	 * @throws std::runtime_error for anything posedBounds refuses (a clip set cooked against
	 *         another rig, a malformed vertex layout, a bad joint index), or anything solePlanes
	 *         refuses when `legs` is given.
	 */
	[[nodiscard]] std::vector<float>
	measureClipFloors(
		const AnimationSet&             animations,
		std::span<const BMesh>          meshes,
		const Skeleton&                 skeleton,
		std::span<const AvatarLegChain> legs = {});

	/**
	 * Move each clip of `animations` down by the floor measureClipFloors reports, so the lowest `y`
	 * `meshes` reach over it rests on 0 -- or, for a rig whose `legs` are given, so its lowest sole
	 * does -- recording the move in `AnimationClip::groundOffset`.
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
		AnimationSet&                   animations,
		std::span<const BMesh>          meshes,
		const Skeleton&                 skeleton,
		std::span<const ClipFloor>      authored = {},
		std::span<const AvatarLegChain> legs     = {});

	/**
	 * How close a sole must sit to the grounded floor to count as planted. Two centimetres: below a
	 * boot sole's own thickness, and above the millimetre a fitted plane and a quantized weight
	 * disagree by.
	 */
	inline constexpr float c_PlantHeightEpsilon = 0.02f;

	/**
	 * How far a sole may drift from the clip's stance motion across the frame either side of it and
	 * still count as planted, when the stance is still. Two centimetres over a 15th of a second at
	 * the default rate -- a third of a metre a second, which is a shuffle rather than a stride.
	 */
	inline constexpr float c_PlantSlideEpsilon = 0.02f;

	/**
	 * How far a clip's lowest sole may sit above the ground it was rested on and still be standing
	 * on it. groundClips rests a clip's lowest *vertex* on zero, and in a walk that is a toe tip
	 * dipping through the floor mid-swing -- the standing foot then sits a few centimetres up, and
	 * measuring against its own lowest point is what finds it. Ten centimetres, which a dipping
	 * toe stays under and a foot hovering beside a seated or lying rig does not: past this, nothing
	 * in the clip is standing, and no foot in it plants.
	 */
	inline constexpr float c_PlantFloorSlack = 0.10f;

	/**
	 * How far above the clip's lowest sole a foot's own floor may sit: a cocked pelvis lifts one
	 * leg 3 cm (the Dog's idle), a knee held up lifts it 8 cm (the Dog's jump).
	 */
	inline constexpr float c_PlantFloorSpread = 0.05f;

	/**
	 * The same drift as a fraction of the stance motion, for a clip whose stance moves: the
	 * standing foot of a fast clip drifts a fraction of its stride, and a fraction is what a rule
	 * about sliding should read. A third, which the Coyote's run -- a tenth -- clears and a foot
	 * dragged at half the stride does not.
	 */
	inline constexpr float c_PlantSlideFraction = 0.33f;

	/**
	 * How far above a foot's lowest vertex the sole reaches: the band of vertices `solePlanes`
	 * fits its plane through. A centimetre -- a pad is flat to a few millimetres, and the next
	 * thing up a standing leg is the instep, which is not.
	 */
	inline constexpr float c_SoleBand = 0.01f;

	/**
	 * Frames a planted run is ramped in and out over. Three, because one is a 33 ms step at 30 Hz
	 * and reads as a pop; the foot is fully planted two frames into the run.
	 */
	inline constexpr uint32_t c_PlantRampFrames = 3;

	/**
	 * The plane one foot stands on, in its ankle's own space: a point on the sole and the outward
	 * normal, which is the form `bgl::FootPlantLegDesc` takes.
	 *
	 * Ankle-local because that is the frame the plant solve turns the foot in -- see
	 * docs/skinning.md. Model space would be a bind-pose fact the moment the ankle rotated.
	 */
	struct SolePlane
	{
		glm::vec3 point;
		glm::vec3 normal;
	};

	/**
	 * The sole plane of each of `chains`, fitted to the underside of the vertices `meshes` weight to
	 * that leg's ankle and toe at bind pose.
	 *
	 * Derived and never authored: an authored plane per foot is a chore on 29 purchased rigs, and it
	 * is the kind of chore that leaves a feature unused. It costs one pass over the vertex
	 * influences, which a load already makes.
	 *
	 * A property of a (mesh, skeleton) pairing rather than of the rig alone -- the vertices are the
	 * mesh's -- which is why it is measured here and lives in no container. Several meshes because a
	 * rig drawn as a body and a separately imported boot has its sole on whichever carries the foot.
	 *
	 * Fitted as `y = ax + bz + d` over the band of vertices within `c_SoleBand` of the lowest --
	 * what touches the ground -- rather than by a general least-squares plane over the foot: a
	 * sole in bind pose is within a few degrees of horizontal, so this parameterisation is well
	 * conditioned and needs no eigen solve, and a foot that stood vertically would have no sole to
	 * speak of.
	 *
	 * A leg no mesh here has weight on gets the flat plane through the ankle -- the only answer that
	 * does not invent one -- exactly as an unweighted clip measures a floor of 0.
	 *
	 * @throws std::runtime_error for anything decodeInfluences refuses (a malformed vertex layout, a
	 *         joint index naming no bone), or if a chain names a bone outside `skeleton`.
	 */
	[[nodiscard]] std::vector<SolePlane>
	solePlanes(
		std::span<const BMesh>          meshes,
		const Skeleton&                 skeleton,
		std::span<const AvatarLegChain> chains);

	/**
	 * What a baked plant weight was measured against: the rig, the avatar resolved on it -- legs
	 * and unplanted clips both -- and the geometry the soles were fitted to. A re-imported mesh, a
	 * re-authored bind or an edited avatar changes it; renames and material swaps do not.
	 *
	 * The clips are deliberately absent: they live in the same file, so a clip edit rewrites the
	 * weights beside it and a signature over them would only be a second way to say so.
	 */
	[[nodiscard]] uint64_t
	plantWeightsSignature(
		std::span<const BMesh> meshes,
		const Skeleton&        skeleton,
		const ResolvedAvatar&  avatar) noexcept;

	/**
	 * How planted each leg is in each frame of `animations`: one byte per leg per frame, frame-major
	 * over the whole sample pool. The rule, and why each part is what it is, is in docs/skinning.md.
	 *
	 * Planted: sole within `c_PlantHeightEpsilon` of the foot's own floor, and moving with the
	 * clip's stance (median motion of soles at floor level over the frame either side) within
	 * `c_PlantSlideEpsilon` or `c_PlantSlideFraction` of it.
	 * Floor: the lowest that sole gets in the clip. No floor when the clip's lowest sole is above
	 * `c_PlantFloorSlack`, or the foot's own is more than `c_PlantFloorSpread` above the clip's.
	 * Ramp: `c_PlantRampFrames` in and out of each run, not at a clip's edge.
	 * Zero throughout: a clip the avatar names in `unplanted`.
	 * Skipped: a clip whose first sample is off a frame boundary.
	 *
	 * @throws std::runtime_error for anything poseModelTransforms refuses -- above all a clip set
	 *         cooked against another rig.
	 */
	[[nodiscard]] std::vector<uint8_t>
	measurePlantWeights(
		const AnimationSet&        animations,
		const Skeleton&            skeleton,
		const ResolvedAvatar&      avatar,
		std::span<const SolePlane> soles);

	/**
	 * Measure the plant weights for `avatar` on `meshes` and store them on `animations`, keyed by
	 * plantWeightsSignature -- what a cook runs after grounding, so no load has to.
	 *
	 * One measurement, not one per mesh: the legs are the rig's, so a rig drawn as several meshes
	 * plants the same feet. The meshes are here for the soles alone.
	 *
	 * Whatever was stored is replaced: unlike a posed box, which is per mesh entry and so shares the
	 * file with other meshes' boxes, a clip set has exactly one set of legs.
	 *
	 * A measurement the walk refuses leaves the file without weights rather than failing the cook:
	 * the weights are derived data and a load measures, exactly as it would had this never run.
	 */
	void
	bakePlantWeights(
		AnimationSet&          animations,
		std::span<const BMesh> meshes,
		const Skeleton&        skeleton,
		const ResolvedAvatar&  avatar);

	/**
	 * The weights bakePlantWeights stored for this pairing, or nullopt where the `.banim` carries
	 * none -- never baked, a different rig or mesh, or a source that changed since. The caller
	 * measures those, and weights that are there are trusted verbatim.
	 *
	 * The leg count is checked against `avatar` as well as the signature: a signature match with a
	 * different count would index the wrong leg, and reading past the end is not the failure to
	 * settle for.
	 *
	 * Costs the same whole-vertex-pool hash `findPosedBounds` pays, so a load asking both questions
	 * hashes the same bytes twice -- ~30 ms a rig, measured in docs/skinning.md. Worth knowing
	 * before putting either inside a per-entry loop.
	 */
	[[nodiscard]] std::optional<std::vector<uint8_t>>
	findPlantWeights(
		const AnimationSet&    animations,
		std::span<const BMesh> meshes,
		const Skeleton&        skeleton,
		const ResolvedAvatar&  avatar);

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
