#pragma once

#include <assetlib/asset_refs.h>
#include <string>
#include <string_view>
#include <vector>
namespace editor
{
	/** A rigged mesh's animation sources: the rig it names, and the clip files authored against it. */
	struct AnimationBindings
	{
		std::string              skeleton;    // data-root-relative, as the mesh records it
		std::vector<std::string> animations;  // data-root-relative .banim candidates, sorted
	};

	/**
	 * What a mesh skinned to `skeleton` can play: every clip set in `graph` authored against that
	 * rig -- a query over its `kClipSkeleton` edges, so matching, path normalization and the error
	 * policy have one home. The caller passes the path its already-loaded mesh records rather than
	 * this re-reading the file, which a stale-but-regenerable mesh would refuse. Candidates come
	 * back sorted, so which one a caller picks first does not depend on scan order. An empty
	 * `skeleton` (a static mesh) resolves to no candidates.
	 *
	 * `graph` is passed in rather than scanned here because a scan reads and parses every asset in
	 * the project, and a caller that also wants the blend sets (`ResolveBlendSets`) would otherwise
	 * pay for two of them on every load.
	 *
	 * Signature drift is deliberately not checked here: a candidate whose clips no longer match
	 * the rig is refused by the bake, which names the reason.
	 */
	[[nodiscard]] AnimationBindings
	ResolveAnimationBindings(const assetlib::AssetRefGraph& graph, std::string_view skeleton);
}
