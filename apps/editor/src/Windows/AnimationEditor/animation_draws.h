#pragma once

#include <assetlib/AssetStore.h>

#include "Mesh/BMeshUtil.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/animation_source.h"

#include <gamelib/AssetManager.h>
#include <gamelib/PreparedMesh.h>

namespace editor
{
	/**
	 * PlanInstances split by how each placement draws: a skinned mesh entry animates (as VAT
	 * today; the skinned tier will fill the same vector), anything else stands as static
	 * geometry. One mesh can hold both -- a rigged character with a static prop node.
	 */
	struct AnimationDrawPlan
	{
		std::vector<bmesh::InstancePlacement> animated;
		std::vector<bmesh::InstancePlacement> statics;
	};

	[[nodiscard]] AnimationDrawPlan
	PlanAnimationDraws(const assetlib::BMesh& mesh);

	/** The acquired clip table in the transport's own terms -- a field-for-field copy. */
	[[nodiscard]] std::vector<ClipInfo>
	ToClipInfos(std::span<const game::ClipInfo> clips);

	/**
	 * What a load has to do differently for each tier. Every decision follows from the source, kept
	 * together so they cannot drift apart -- the panel reads them once rather than testing the source
	 * at three points of a long function.
	 */
	struct AnimationLoadSteps
	{
		// The VAT tier draws from a bake and must find one already fresh -- a load never makes one,
		// because a bake is seconds of CPU skinning and therefore the user's decision. The skinned
		// tier reads the rig, so a bake would be that cost for a texture pair nothing samples.
		bool needsFreshBake = false;

		// Where the posed box comes from: the bake closed over one for the whole file, so VAT reads
		// it, while the skinned tier measures one per mesh entry. That box frames the camera *and*
		// culls the geom, so getting it from the wrong place hides the mesh rather than mis-aiming
		// the camera.
		bool framedByBake = false;
	};

	/**
	 * The mesh's materials that a bake would change, by the same verdict gamelib routes on --
	 * `DrawsLoose` covers never-baked *and* stale-by-stamp, since an edited source drifts from the
	 * triplet baked off it and only a re-bake closes the gap.
	 *
	 * Empty means a bake cannot answer this refusal: a material with no routes has nothing to bake,
	 * and one already drawing its baked triplet was refused for another reason. That is what decides
	 * whether the panel offers to bake, so it is a rule rather than a detail of the dialog.
	 *
	 * A material that will not load is skipped, not thrown on: the refusal is already being reported
	 * and a second failure on top of it helps nobody.
	 */
	[[nodiscard]] std::vector<std::string>
	BakeableMaterials(const assetlib::AssetStore& store, std::span<const std::string> materials);

	[[nodiscard]] AnimationLoadSteps
	PlanAnimationLoad(AnimationSource source, bool hasAnimations);

	/** One placement of an AnimationDrawPlan with its acquire done bar the upload. */
	struct PreparedAnimationDraw
	{
		// Move-only, because game::PreparedMesh is; see the note there.
		PreparedAnimationDraw()  = default;
		~PreparedAnimationDraw() = default;

		PreparedAnimationDraw(PreparedAnimationDraw&&) = default;

		PreparedAnimationDraw&
		operator=(PreparedAnimationDraw&&) = default;

		PreparedAnimationDraw(const PreparedAnimationDraw&) = delete;

		PreparedAnimationDraw&
		operator=(const PreparedAnimationDraw&) = delete;

		bmesh::InstancePlacement placement;
		game::PreparedMesh       prepared;

		// False when this was prepared as static geometry: the node is static, the rig has no clips
		// to play, or the tier refused it below. The commit takes the static door for it.
		bool animated = false;

		// The box the geom culls by, on the animated tiers. Nullopt falls back to the bind pose's.
		std::optional<assetlib::Bounds> posed;

		// Why the tier refused this placement, dropping it to bind-pose static geometry. Empty when
		// nothing refused it.
		std::string refusal;
	};

	/**
	 * Every acquire a load of `plan` will make, less the upload -- the whole of what the panel used
	 * to do on the render thread. Reads through `store` and touches no scene, so it belongs on the
	 * loading screen's worker; the commit that follows uploads and nothing else.
	 *
	 * A placement the animated tier refuses is prepared as static geometry instead, carrying the
	 * reason: a mesh standing in its bind pose beats a viewport cleared to nothing. The refusals the
	 * *commit* owns -- a submesh whose material does not resolve to kPBR needs the scene's material
	 * handles -- cannot be seen from here, so a caller still handles one there.
	 *
	 * @param animationsRelPath The clip set both animated tiers read. Empty stands every placement
	 *        as static geometry: a rig with no clips anywhere has nothing to play.
	 * @param animatedBounds One entry per `plan.animated`, in order, or empty for none known.
	 */
	[[nodiscard]] std::vector<PreparedAnimationDraw>
	PrepareAnimationDraws(
		const assetlib::AssetStore&                      store,
		std::string_view                                 relPath,
		std::string_view                                 animationsRelPath,
		AnimationSource                                  source,
		const AnimationDrawPlan&                         plan,
		std::span<const std::optional<assetlib::Bounds>> animatedBounds = {});
}
