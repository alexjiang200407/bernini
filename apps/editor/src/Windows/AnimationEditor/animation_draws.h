#pragma once

#include <assetlib/AssetStore.h>

#include "Mesh/BMeshUtil.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/animation_source.h"

#include <gamelib/AssetManager.h>

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

	/**
	 * Whether the bone overlay can be shown for what the panel is currently previewing, and why not
	 * when it cannot -- the reason goes in the checkbox's tooltip, because a control that greys
	 * itself without saying why reads as broken.
	 *
	 * Both refusals are about there being no rig on the GPU to draw: a `.bvat` keeps its baked
	 * palettes in the container and nothing on the GPU reads them, and a mesh with no clip file
	 * loads as static geometry with no palette at all (see PlanAnimationLoad).
	 */
	struct BoneOverlayOffer
	{
		bool allowed = false;

		// Empty when `allowed`; otherwise one sentence, shown as the tooltip.
		std::string_view refusal;
	};

	[[nodiscard]] BoneOverlayOffer
	OfferBoneOverlay(AnimationSource source, bool hasAnimations);
}
