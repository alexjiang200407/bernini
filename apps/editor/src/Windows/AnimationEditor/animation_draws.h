#pragma once

#include <assetlib/AssetStore.h>

#include "Mesh/BMeshUtil.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include <gamelib/ClipInfo.h>

#include <gamelib/AssetManager.h>
#include <span>
#include <string>
#include <vector>

namespace editor
{
	/**
	 * PlanInstances split by how each placement draws: a skinned mesh entry animates, anything else
	 * stands as static geometry. One mesh can hold both -- a rigged character with a static prop
	 * node. Both pose sources fill the same vector, being one upload and one geom.
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

}
