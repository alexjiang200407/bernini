#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <core/err/util.h>
#include <core/hash.h>

namespace assetlib
{
	using core::throw_runtime_error;

	// Persisted in `.banim`, so a change to core's hash invalidates every file already written and
	// needs a major version bump with it.
	uint64_t
	skeletonSignature(const Skeleton& skeleton) noexcept
	{
		uint64_t hash = core::hash_seed();
		for (const Bone& bone : skeleton.bones)
		{
			hash = core::hash_string(skeleton.stringPool.at(bone.nameOffset), hash);
			hash = core::hash_pod(bone.parent, hash);
		}
		return hash;
	}

	void
	validateSkeleton(const Skeleton& skeleton)
	{
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const Bone& bone = skeleton.bones[i];

			if (bone.parent != c_InvalidIndex && bone.parent >= i)
				throw_runtime_error(
					"skeleton: bone {} has parent {}, which is not before it -- the bones are not "
					"topologically sorted",
					i,
					bone.parent);

			if (bone.nameOffset != 0 && skeleton.stringPool.at(bone.nameOffset).empty())
				throw_runtime_error("skeleton: bone {} names nothing in the string pool", i);
		}
	}

	void
	validateAnimationSet(const AnimationSet& animations)
	{
		if (animations.boneCount == 0)
		{
			if (!animations.samples.empty())
				throw_runtime_error("animation: samples with no bones to address them by");
			return;
		}

		if (animations.samples.size() % animations.boneCount != 0)
			throw_runtime_error("animation: the sample pool is not a whole number of poses");

		for (size_t i = 0; i < animations.clips.size(); ++i)
		{
			const AnimationClip& clip = animations.clips[i];

			if (clip.frameCount == 0)
				throw_runtime_error("animation: clip {} has no frames", i);

			const size_t end = static_cast<size_t>(clip.firstSample) +
			                   static_cast<size_t>(clip.frameCount) * animations.boneCount;
			if (end > animations.samples.size())
				throw_runtime_error("animation: clip {} samples past the end of the pool", i);
		}
	}

	std::optional<uint32_t>
	findBone(const Skeleton& skeleton, std::string_view name)
	{
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			if (skeleton.stringPool.at(skeleton.bones[i].nameOffset) == name)
				return static_cast<uint32_t>(i);

		return std::nullopt;
	}

	namespace
	{
		void
		requireSameRig(const AnimationSet& animations, const Skeleton& skeleton)
		{
			// Bone count *and* signature: a rig re-exported with two bones swapped keeps the count
			// and changes the signature, and a pose evaluated against it is wrong in a way no later
			// check can see. This is the first place the two files meet per-pose, which is what the
			// signature was written for.
			if (!animationsMatchSkeleton(animations, skeleton))
				throw_runtime_error(
					"animation: clips cooked for {} bones against a different rig than this "
					"{}-bone skeleton",
					animations.boneCount,
					skeleton.bones.size());
		}

		const AnimationClip&
		requireClip(const AnimationSet& animations, uint32_t clip)
		{
			if (clip >= animations.clips.size())
				throw_runtime_error(
					"animation: clip {} of a set that holds {}",
					clip,
					animations.clips.size());
			if (animations.clips[clip].frameCount == 0)
				throw_runtime_error("animation: clip {} has no frames", clip);
			return animations.clips[clip];
		}

		/** The pool index of bone 0 at `frame` of `clip`, checked against the pool it addresses. */
		size_t
		frameBase(const AnimationSet& animations, uint32_t clip, uint32_t frame)
		{
			const AnimationClip& entry = animations.clips[clip];
			// Both containers validate on load, so this catches a set built in memory rather than
			// read from disk -- and costs one comparison against reading past the pool.
			const size_t base = static_cast<size_t>(entry.firstSample) +
			                    static_cast<size_t>(frame) * animations.boneCount;
			if (base + animations.boneCount > animations.samples.size())
				throw_runtime_error("animation: clip {} samples past the end of the pool", clip);
			return base;
		}

		std::vector<glm::mat4>
		walk(const Skeleton& skeleton, std::span<const Transform> locals)
		{
			std::vector<glm::mat4> model(skeleton.bones.size());
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const auto     local  = toMatrix(locals[i]);
				const uint32_t parent = skeleton.bones[i].parent;
				model[i]              = parent == c_InvalidIndex ? local : model[parent] * local;
			}
			return model;
		}

		Transform
		nlerp(const Transform& a, const Transform& b, float t) noexcept
		{
			const glm::quat rotB =
				glm::dot(a.rotation, b.rotation) < 0.0f ? -b.rotation : b.rotation;
			return Transform{ glm::mix(a.translation, b.translation, t),
				              glm::normalize(a.rotation * (1.0f - t) + rotB * t),
				              glm::mix(a.scale, b.scale, t) };
		}

		/** Every bone's local pose at a fractional frame of one clip. */
		std::vector<Transform>
		sampleClip(const AnimationSet& animations, const BlendSample& sample)
		{
			const AnimationClip& entry = requireClip(animations, sample.clip);
			const float          last  = static_cast<float>(entry.frameCount - 1);
			if (!(sample.frames >= 0.0f && sample.frames <= last))
				throw_runtime_error(
					"animation: frame {} of a clip that spans [0, {}]",
					sample.frames,
					last);

			const auto  f0 = static_cast<uint32_t>(sample.frames);
			const auto  f1 = std::min(f0 + 1, entry.frameCount - 1);
			const float t  = sample.frames - static_cast<float>(f0);

			const size_t base0 = frameBase(animations, sample.clip, f0);
			const size_t base1 = frameBase(animations, sample.clip, f1);

			std::vector<Transform> locals(animations.boneCount);
			for (size_t i = 0; i < locals.size(); ++i)
				locals[i] = nlerp(animations.samples[base0 + i], animations.samples[base1 + i], t);
			return locals;
		}
	}

	std::vector<glm::mat4>
	poseModelTransforms(
		const Skeleton&     skeleton,
		const AnimationSet& animations,
		uint32_t            clip,
		uint32_t            frame)
	{
		const AnimationClip& entry = requireClip(animations, clip);
		if (frame >= entry.frameCount)
			throw_runtime_error(
				"animation: frame {} of a clip that holds {}",
				frame,
				entry.frameCount);

		requireSameRig(animations, skeleton);

		const size_t base = frameBase(animations, clip, frame);
		return walk(skeleton, std::span(animations.samples).subspan(base, animations.boneCount));
	}

	std::vector<glm::mat4>
	poseModelTransforms(
		const Skeleton&              skeleton,
		const AnimationSet&          animations,
		std::span<const BlendSample> blend)
	{
		if (blend.empty())
			throw_runtime_error("animation: a blend of no samples");

		float total = 0.0f;
		for (const BlendSample& sample : blend)
		{
			if (!(sample.weight >= 0.0f) || !std::isfinite(sample.weight))
				throw_runtime_error("animation: a blend weight of {}", sample.weight);
			total += sample.weight;
		}
		if (total <= 0.0f)
			throw_runtime_error("animation: blend weights that sum to zero");

		requireSameRig(animations, skeleton);

		std::vector<std::vector<Transform>> locals;
		locals.reserve(blend.size());
		for (const BlendSample& sample : blend) locals.emplace_back(sampleClip(animations, sample));

		std::vector<Transform> blended(animations.boneCount);
		for (size_t bone = 0; bone < blended.size(); ++bone)
		{
			glm::vec3 translation(0.0f);
			glm::vec3 scale(0.0f);
			glm::quat rotation(0.0f, 0.0f, 0.0f, 0.0f);
			for (size_t s = 0; s < blend.size(); ++s)
			{
				const float      weight = blend[s].weight / total;
				const Transform& local  = locals[s][bone];
				translation += weight * local.translation;
				scale += weight * local.scale;
				// Against the running sum rather than a fixed reference, so the sign decision is
				// continuous in the weights: a crossfade passing 50 % flips nothing.
				const glm::quat aligned =
					glm::dot(rotation, local.rotation) < 0.0f ? -local.rotation : local.rotation;
				rotation += aligned * weight;
			}
			blended[bone] = Transform{ translation, glm::normalize(rotation), scale };
		}

		return walk(skeleton, blended);
	}

	std::vector<glm::mat4>
	skinningMatrices(const Skeleton& skeleton, std::span<const glm::mat4> modelTransforms)
	{
		if (modelTransforms.size() != skeleton.bones.size())
			throw_runtime_error(
				"skeleton: {} model transforms for {} bones",
				modelTransforms.size(),
				skeleton.bones.size());

		std::vector<glm::mat4> skinning(skeleton.bones.size());
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
			skinning[i] = modelTransforms[i] * skeleton.bones[i].inverseBind;

		return skinning;
	}

	std::vector<glm::mat4>
	bindPoseModelTransforms(const Skeleton& skeleton)
	{
		std::vector<glm::mat4> model(skeleton.bones.size());
		for (size_t i = 0; i < skeleton.bones.size(); ++i)
		{
			const Bone& bone  = skeleton.bones[i];
			const auto  local = toMatrix(bone.bindPose);
			model[i]          = bone.parent == c_InvalidIndex ? local : model[bone.parent] * local;
		}
		return model;
	}
}
