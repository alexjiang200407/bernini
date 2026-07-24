#pragma once
#include <assetlib_structs/Node.h>

namespace assetlib
{
	/**
	 * One joint of a skeleton.
	 *
	 * Bones are stored in topological order -- `parent` is always less than the bone's own index --
	 * so a local-to-model hierarchy walk is a single forward pass, with no sort, queue or recursion
	 * at runtime.
	 */
	struct Bone
	{
		Transform bindPose;     // local TRS relative to `parent` at bind time
		glm::mat4 inverseBind;  // model space -> bone space
		uint32_t  parent;       // c_InvalidIndex for a root
		uint32_t  nameOffset;   // byte offset into Skeleton::stringPool (0 == empty)
	};

	static_assert(sizeof(Bone) == 112);

	/**
	 * A rig. A mesh's joint indices and an animation clip's samples both index `bones` directly, so
	 * the three are only meaningful together -- see skeletonSignature, which is what catches them
	 * having drifted apart.
	 */
	struct Skeleton
	{
		std::vector<Bone> bones;
		std::vector<char> stringPool;  // NUL-terminated names; offset 0 is the empty string
	};
}
