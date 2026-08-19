#pragma once

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

		// Only a VAT refusal is a bake's to answer. A skinned refusal is about the rig or the
		// material, and offering to bake would send the user somewhere that cannot help.
		bool offerBakeOnRefusal = false;

		// Where the posed box comes from: the bake closed over one for the whole file, so VAT reads
		// it, while the skinned tier measures one per mesh entry. That box frames the camera *and*
		// culls the geom, so getting it from the wrong place hides the mesh rather than mis-aiming
		// the camera.
		bool framedByBake = false;
	};

	[[nodiscard]] AnimationLoadSteps
	PlanAnimationLoad(AnimationSource source, bool hasAnimations);
}
