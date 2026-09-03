#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * One leg of a rig, ready for the pose pass: bone indices into the skeleton uploaded beside it,
	 * and the plane its sole rests on.
	 *
	 * The four bones must form a direct chain -- `knee`'s parent is `hip`, `ankle`'s is `knee`,
	 * `toe`'s is `ankle`. The solve rewrites those slots and carries their descendants rigidly, so a
	 * bone in between would be left holding a pose the joints above it no longer agree with.
	 *
	 * bgl never reads a `.bavatar` and never measures a sole: resolving a bone name needs the
	 * skeleton's string pool and fitting a plane needs the mesh's vertex layout, and both live in
	 * assetlib. Whoever loaded the containers fills this in -- gamelib's acquire.
	 */
	struct FootPlantLegDesc
	{
		uint32_t hip   = 0;
		uint32_t knee  = 0;
		uint32_t ankle = 0;
		uint32_t toe   = 0;

		// Ankle-local -- in bone units, which carry whatever scale the bind does -- and normalized on
		// upload.
		glm::vec3 solePoint  = glm::vec3(0.0f);
		glm::vec3 soleNormal = glm::vec3(0.0f, 1.0f, 0.0f);
	};

	/**
	 * What a rig needs to plant its feet, or nothing at all -- the default is a rig that animates
	 * exactly as it did before foot planting existed, which is every rig whose skeleton has no
	 * `.bavatar`.
	 *
	 * `legs` and `plantWeights` stand or fall together: a leg with no weights would be solved on
	 * every frame of every clip, including the ones it is mid-swing on. A rig that is never planted
	 * is spelled as weights of zero, not as absent ones.
	 */
	struct FootPlantDesc
	{
		std::vector<FootPlantLegDesc> legs;

		/**
		 * How planted each leg is in each frame of the clip set's sample pool, 0 (free) to 255
		 * (fully planted), frame-major: leg `l` of frame `f` is `plantWeights[f * legs.size() + l]`.
		 * A weight rather than a flag because a foot that snapped between the two states would pop;
		 * the cook ramps it over the frames at each end of a planted run.
		 *
		 * Empty exactly when `legs` is; otherwise `legs.size()` entries for every frame in the pool.
		 */
		std::vector<uint8_t> plantWeights;
	};
}
