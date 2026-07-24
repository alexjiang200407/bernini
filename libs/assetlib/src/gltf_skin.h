#pragma once
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <tiny_gltf.h>

namespace assetlib
{
	/** A glTF skin, turned into a skeleton and the index remaps that survive the reordering. */
	struct SkinImport
	{
		Skeleton skeleton;

		/** glTF node index -> bone index, c_InvalidIndex for a node that is not a joint. */
		std::vector<uint32_t> nodeToBone;

		/** Position in `skin.joints` -> bone index. This is what a mesh's JOINTS_0 indexes by. */
		std::vector<uint32_t> jointToBone;
	};

	/**
	 * The document's skin as a topologically sorted skeleton (`parent < index`), so that a runtime
	 * hierarchy walk is one forward pass.
	 *
	 * Bone order is *not* `skin.joints` order, which is arbitrary -- hence `jointToBone`, without
	 * which a mesh's joint indices would name the wrong bones.
	 *
	 * @return An empty import when the document has no skin.
	 * @throws std::runtime_error if the document has more than one skin -- one file is one rig here,
	 *         and taking the first would silently bind a mesh to the wrong bones -- or if a skin's
	 *         joints do not form a tree.
	 */
	[[nodiscard]] SkinImport
	importSkin(const tinygltf::Model& model);

	/**
	 * The document's animations, resampled to `sampleRate` and expressed against `skin`'s bones.
	 *
	 * Every clip is uniformly sampled over its own [0, duration], which is what removes the keyframe
	 * search from the runtime. A bone no channel targets holds its bind pose; an animation that
	 * targets no bone at all is not a clip of this rig and is left out.
	 *
	 * `skeletonSignature` and `boneCount` are filled in; `skeleton` is not -- the path is the
	 * caller's to choose, and is assigned when the set is written.
	 *
	 * @throws std::runtime_error if a sampler uses an interpolation or accessor type glTF does not
	 *         define for animation.
	 */
	[[nodiscard]] AnimationSet
	importAnimations(const tinygltf::Model& model, const SkinImport& skin, float sampleRate);
}
