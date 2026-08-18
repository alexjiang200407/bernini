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
	 * What a load has to do differently for each tier. Both decisions follow from the source, kept
	 * together so they cannot drift apart -- the panel reads them once rather than testing the source
	 * at two points of a long function.
	 */
	struct AnimationLoadSteps
	{
		// The VAT tier draws from a bake and must find it fresh; the skinned tier reads the rig, so
		// baking for it is seconds of CPU skinning for a texture pair nothing samples.
		bool bakeVat = false;

		// Only a VAT refusal is a bake's to answer. A skinned refusal is about the rig or the
		// material, and offering to bake would send the user somewhere that cannot help.
		bool offerBakeOnRefusal = false;
	};

	[[nodiscard]] AnimationLoadSteps
	PlanAnimationLoad(AnimationSource source, bool hasAnimations);

	/**
	 * The orbit yaw that puts the camera in front of a rig's face, or nothing when the skeleton does
	 * not say which way it faces.
	 *
	 * A fixed yaw cannot do this. Authoring conventions disagree on a rig's forward axis -- the test
	 * coyote faces +X where glTF's own convention is +Z -- so any constant shows some rigs a profile.
	 * The skeleton does know: a head looks toward its own children, which are what a face is made of.
	 *
	 * Root-to-head is the obvious reading and it is wrong: a rig whose clip rears it up puts the head
	 * above the root rather than in front of it, leaving only noise in the horizontal part.
	 *
	 * The head is found by name, because nothing else distinguishes it from any other bone at the end
	 * of a chain. A rig whose bones are unnamed, or named in another language, or whose head has no
	 * children, gets nothing back rather than a wrong guess -- the caller keeps its default.
	 */
	[[nodiscard]] std::optional<float>
	RestFacingYaw(const assetlib::Skeleton& skeleton, const assetlib::AnimationSet& animations);
}
