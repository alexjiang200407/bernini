#pragma once

namespace editor
{
	/** A rigged mesh's animation sources: the rig it names, and the clip files authored against it. */
	struct AnimationBindings
	{
		std::string              skeleton;    // data-root-relative, as the mesh records it
		std::vector<std::string> animations;  // data-root-relative .banim candidates, sorted
	};

	/**
	 * What the mesh at `meshRelPath` can play: the skeleton path the mesh records, and every clip
	 * set in the project authored against the same rig -- a query over the reference graph
	 * assetlib already scans (its `kClipSkeleton` edges), so matching, path normalization and the
	 * error policy have one home. Candidates come back sorted, so which one a caller picks first
	 * does not depend on scan order. A static mesh (no skeleton) resolves to no candidates
	 * without a scan.
	 *
	 * Signature drift is deliberately not checked here: a candidate whose clips no longer match
	 * the rig is refused by the bake, which names the reason.
	 *
	 * @throws std::runtime_error if the mesh cannot be read, or the reference scan refuses the
	 *         project -- an unreadable referrer is fatal there, as it is for the prune.
	 */
	[[nodiscard]] AnimationBindings
	ResolveAnimationBindings(const std::filesystem::path& dataRoot, std::string_view meshRelPath);
}
