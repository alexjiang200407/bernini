#pragma once
#include <cstdint>
#include <optional>

namespace assetlib
{
	struct BToonShadingRig;
	struct Skeleton;

	/**
	 * The bone `rig.headBone` names, as an index into `skeleton`, or nullopt when it names none --
	 * a rig whose head is the placement's own frame.
	 *
	 * Resolved where the rig meets the mesh's skeleton and nowhere else, as an avatar's legs are:
	 * the document keeps the name, so a rig re-imported under a reordered bone table resolves afresh.
	 *
	 * @throws std::runtime_error naming the bone if `skeleton` carries no bone of that name.
	 */
	[[nodiscard]] std::optional<uint32_t>
	resolveHeadBone(const BToonShadingRig& rig, const Skeleton& skeleton);
}
