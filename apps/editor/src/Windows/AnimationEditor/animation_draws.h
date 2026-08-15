#pragma once

#include "Mesh/BMeshUtil.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"

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
	ToClipInfos(std::span<const game::AssetManager::VatClipInfo> clips);
}
