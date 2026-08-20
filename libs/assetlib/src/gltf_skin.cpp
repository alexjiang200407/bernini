#include "gltf_skin.h"

#include <assetlib/skeleton.h>

#include "gltf_util.h"

#include <core/err/util.h>

namespace assetlib
{
	using core::throw_runtime_error;

	namespace
	{
		/**
		 * The joints' nearest-joint-ancestor forest, emitted parents-first.
		 *
		 * glTF permits ordinary nodes between two joints, so a joint's bone parent is its nearest
		 * ancestor that is *also* a joint rather than its node parent.
		 */
		std::vector<uint32_t>
		topologicalJointOrder(
			std::span<const int>      joints,
			std::span<const uint32_t> nodeParents,
			std::span<const uint32_t> nodeToJoint,
			std::vector<uint32_t>&    jointParents)
		{
			jointParents.assign(joints.size(), c_InvalidIndex);

			std::vector<std::vector<uint32_t>> children(joints.size());
			std::vector<uint32_t>              roots;

			for (size_t j = 0; j < joints.size(); ++j)
			{
				uint32_t ancestor = nodeParents[static_cast<size_t>(joints[j])];
				while (ancestor != c_InvalidIndex && nodeToJoint[ancestor] == c_InvalidIndex)
					ancestor = nodeParents[ancestor];

				if (ancestor == c_InvalidIndex)
				{
					roots.push_back(static_cast<uint32_t>(j));
					continue;
				}

				jointParents[j] = nodeToJoint[ancestor];
				children[nodeToJoint[ancestor]].push_back(static_cast<uint32_t>(j));
			}

			// Depth-first from each root in skin.joints order, so the bone order a given file produces
			// is the same every time it is imported.
			std::vector<uint32_t> order;
			order.reserve(joints.size());

			std::vector<uint32_t> stack(roots.rbegin(), roots.rend());
			while (!stack.empty())
			{
				const uint32_t joint = stack.back();
				stack.pop_back();
				order.push_back(joint);

				const auto& kids = children[joint];
				stack.insert(stack.end(), kids.rbegin(), kids.rend());
			}

			if (order.size() != joints.size())
				throw_runtime_error(
					"bskel: the skin's joints do not form a tree, so they cannot be sorted");

			return order;
		}

		/**
		 * Every node's own local transform, indexed by node. Read once because a `matrix` node
		 * decomposes on every read, and animation asks for these per bone per frame.
		 */
		std::vector<Transform>
		readNodeTransforms(const tinygltf::Model& model)
		{
			std::vector<Transform> poses;
			poses.reserve(model.nodes.size());
			for (const tinygltf::Node& node : model.nodes) poses.push_back(readNodeTransform(node));

			return poses;
		}

		/**
		 * The nodes between `parentNode` (exclusive) and `node`, outermost first. glTF permits
		 * ordinary nodes between two joints, and a bone's transform is the product of all of them.
		 */
		std::vector<uint32_t>
		boneNodeChain(uint32_t node, uint32_t parentNode, std::span<const uint32_t> nodeParents)
		{
			std::vector<uint32_t> chain;
			for (uint32_t cur = node; cur != parentNode; cur = nodeParents[cur])
				chain.push_back(cur);

			std::ranges::reverse(chain);
			return chain;
		}

		/**
		 * A bone's transform relative to its *bone* parent, from the poses of its chain nodes.
		 * A one-node chain is taken untouched rather than round-tripped through a matrix, which is
		 * both exact and the overwhelmingly common case.
		 */
		Transform
		composeBoneTransform(std::span<const uint32_t> chain, std::span<const Transform> nodePoses)
		{
			if (chain.size() == 1)
				return nodePoses[chain[0]];

			glm::mat4 local(1.0f);
			for (const uint32_t node : chain) local = local * toMatrix(nodePoses[node]);

			return decomposeTransform(local);
		}

		enum class Interpolation
		{
			kLinear,
			kStep,
			kCubicSpline
		};

		struct KeyframeSampler
		{
			std::vector<float> times;
			std::vector<float> values;
			int                components    = 0;
			Interpolation      interpolation = Interpolation::kLinear;
		};

		Interpolation
		toInterpolation(std::string_view name)
		{
			if (name == "STEP")
				return Interpolation::kStep;
			if (name == "CUBICSPLINE")
				return Interpolation::kCubicSpline;
			if (name.empty() || name == "LINEAR")
				return Interpolation::kLinear;

			throw_runtime_error("banim: unknown sampler interpolation '{}'", name);
		}

		/**
		 * `sampler` evaluated at `time`, as up to four components. Times outside the sampler's range
		 * clamp to its ends, which is what glTF specifies.
		 */
		glm::vec4
		evaluate(const KeyframeSampler& sampler, float time)
		{
			const size_t keys = sampler.times.size();

			const auto keyValue = [&](size_t key, size_t slot) {
				// A cubic key is (inTangent, value, outTangent); slot 1 is the value itself.
				const size_t stride = sampler.interpolation == Interpolation::kCubicSpline ? 3 : 1;
				const size_t base = (key * stride + slot) * static_cast<size_t>(sampler.components);

				glm::vec4 out(0.0f);
				for (int c = 0; c < sampler.components; ++c) out[c] = sampler.values[base + c];
				return out;
			};

			const size_t valueSlot = sampler.interpolation == Interpolation::kCubicSpline ? 1 : 0;

			if (keys == 1 || time <= sampler.times.front())
				return keyValue(0, valueSlot);
			if (time >= sampler.times.back())
				return keyValue(keys - 1, valueSlot);

			const auto   next = std::ranges::upper_bound(sampler.times, time);
			const size_t k1   = static_cast<size_t>(std::distance(sampler.times.begin(), next));
			const size_t k0   = k1 - 1;

			const float dt = sampler.times[k1] - sampler.times[k0];
			const float u  = dt > 0.0f ? (time - sampler.times[k0]) / dt : 0.0f;

			switch (sampler.interpolation)
			{
			case Interpolation::kStep:
				return keyValue(k0, valueSlot);

			case Interpolation::kLinear:
			{
				const glm::vec4 a = keyValue(k0, 0);
				const glm::vec4 b = keyValue(k1, 0);

				// A rotation must travel the arc, not the chord: lerping four floats shortens the
				// quaternion and drags the joint through a different path between keys.
				if (sampler.components == 4)
				{
					const auto qa = glm::quat(a.w, a.x, a.y, a.z);
					const auto qb = glm::quat(b.w, b.x, b.y, b.z);
					const auto q  = glm::normalize(glm::slerp(qa, qb, u));
					return glm::vec4(q.x, q.y, q.z, q.w);
				}
				return glm::mix(a, b, u);
			}

			case Interpolation::kCubicSpline:
			{
				const glm::vec4 p0 = keyValue(k0, 1);
				const glm::vec4 m0 = keyValue(k0, 2) * dt;
				const glm::vec4 p1 = keyValue(k1, 1);
				const glm::vec4 m1 = keyValue(k1, 0) * dt;

				const float u2 = u * u;
				const float u3 = u2 * u;

				const glm::vec4 value = (2.0f * u3 - 3.0f * u2 + 1.0f) * p0 +
				                        (u3 - 2.0f * u2 + u) * m0 + (-2.0f * u3 + 3.0f * u2) * p1 +
				                        (u3 - u2) * m1;

				if (sampler.components == 4)
				{
					const auto q = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
					return glm::vec4(q.x, q.y, q.z, q.w);
				}
				return value;
			}
			}

			return glm::vec4(0.0f);
		}

		/** The sampler index driving each of a node's three channels, or c_InvalidIndex. */
		struct NodeChannels
		{
			uint32_t translationSampler = c_InvalidIndex;
			uint32_t rotationSampler    = c_InvalidIndex;
			uint32_t scaleSampler       = c_InvalidIndex;
		};

		bool
		posesMatch(std::span<const Transform> a, std::span<const Transform> b) noexcept
		{
			constexpr float c_Epsilon     = 1e-4f;
			constexpr float c_QuatEpsilon = 1e-4f;

			for (size_t i = 0; i < a.size(); ++i)
			{
				if (glm::any(
						glm::greaterThan(
							glm::abs(a[i].translation - b[i].translation),
							glm::vec3(c_Epsilon))))
					return false;
				if (glm::any(
						glm::greaterThan(glm::abs(a[i].scale - b[i].scale), glm::vec3(c_Epsilon))))
					return false;

				// q and -q are the same rotation, so the comparison is on the angle between them.
				if (1.0f - std::abs(glm::dot(a[i].rotation, b[i].rotation)) > c_QuatEpsilon)
					return false;
			}

			return true;
		}

		/** Frames a clip of `duration` occupies at `sampleRate`, both ends included. */
		uint32_t
		frameCountFor(float duration, float sampleRate) noexcept
		{
			if (duration <= 0.0f)
				return 1;

			return static_cast<uint32_t>(std::lround(duration * sampleRate)) + 1u;
		}

		/** The speed a clip was authored at: how far its root travels along the ground per second. */
		float
		locomotionSpeedFor(const glm::vec3& rootMotion, float duration) noexcept
		{
			if (duration <= 0.0f)
				return 0.0f;

			return glm::length(glm::vec2(rootMotion.x, rootMotion.z)) / duration;
		}
	}

	SkinImport
	importSkin(const tinygltf::Model& model)
	{
		if (model.skins.empty())
			return {};

		if (model.skins.size() > 1)
			throw_runtime_error(
				"bskel: the file holds {} skins, and one file is one rig here -- export each rig "
				"separately",
				model.skins.size());

		const tinygltf::Skin& skin = model.skins.front();

		std::vector<uint32_t> nodeToJoint(model.nodes.size(), c_InvalidIndex);
		for (size_t j = 0; j < skin.joints.size(); ++j)
		{
			const int node = skin.joints[j];
			if (node < 0 || static_cast<size_t>(node) >= model.nodes.size())
				throw_runtime_error("bskel: the skin names a joint node that does not exist");
			nodeToJoint[static_cast<size_t>(node)] = static_cast<uint32_t>(j);
		}

		const auto nodeParents = buildNodeParents(model);

		std::vector<uint32_t> jointParents;
		const auto            order =
			topologicalJointOrder(skin.joints, nodeParents, nodeToJoint, jointParents);

		SkinImport out;
		out.nodeToBone.assign(model.nodes.size(), c_InvalidIndex);
		out.jointToBone.assign(skin.joints.size(), c_InvalidIndex);
		for (size_t bone = 0; bone < order.size(); ++bone)
		{
			out.jointToBone[order[bone]] = static_cast<uint32_t>(bone);
			out.nodeToBone[static_cast<size_t>(skin.joints[order[bone]])] =
				static_cast<uint32_t>(bone);
		}

		int        inverseBindComponents = 0;
		const auto inverseBinds =
			readFloatAccessor(model, skin.inverseBindMatrices, inverseBindComponents);
		if (!inverseBinds.empty() && inverseBindComponents != 16)
			throw_runtime_error("bskel: inverseBindMatrices is not a matrix accessor");

		const auto nodePoses = readNodeTransforms(model);

		out.skeleton.bones.reserve(order.size());
		out.boneNodes.reserve(order.size());

		for (const uint32_t joint : order)
		{
			const auto node = static_cast<uint32_t>(skin.joints[joint]);

			Bone     bone{};
			uint32_t parentNode = c_InvalidIndex;

			if (jointParents[joint] == c_InvalidIndex)
			{
				bone.parent = c_InvalidIndex;
			}
			else
			{
				bone.parent = out.jointToBone[jointParents[joint]];
				parentNode  = static_cast<uint32_t>(skin.joints[jointParents[joint]]);
			}

			out.boneNodes.push_back(boneNodeChain(node, parentNode, nodeParents));

			bone.bindPose   = composeBoneTransform(out.boneNodes.back(), nodePoses);
			bone.nameOffset = out.skeleton.stringPool.add(model.nodes[node].name);

			// glTF's default when the accessor is absent: the joints are already in bind pose.
			bone.inverseBind = glm::mat4(1.0f);
			if (const size_t base = static_cast<size_t>(joint) * 16;
			    base + 16 <= inverseBinds.size())
				std::memcpy(&bone.inverseBind, inverseBinds.data() + base, 16 * sizeof(float));

			out.skeleton.bones.push_back(bone);
		}

		validateSkeleton(out.skeleton);
		return out;
	}

	AnimationSet
	importAnimations(const tinygltf::Model& model, const SkinImport& skin, float sampleRate)
	{
		AnimationSet out;
		if (skin.skeleton.bones.empty() || model.animations.empty())
			return out;

		if (sampleRate <= 0.0f)
			throw_runtime_error("banim: the sample rate must be positive");

		const auto boneCount = static_cast<uint32_t>(skin.skeleton.bones.size());

		out.boneCount         = boneCount;
		out.skeletonSignature = skeletonSignature(skin.skeleton);

		const auto authoredPoses = readNodeTransforms(model);

		// A node is a clip's business only if some bone's transform is a product of it. That is every
		// joint, plus the non-joint nodes glTF allowed between one joint and the next.
		std::vector<bool> inBoneChain(model.nodes.size(), false);
		for (const std::vector<uint32_t>& chain : skin.boneNodes)
			for (const uint32_t node : chain) inBoneChain[node] = true;

		for (const tinygltf::Animation& animation : model.animations)
		{
			std::unordered_map<uint32_t, NodeChannels>    channels;
			std::unordered_map<uint32_t, KeyframeSampler> samplers;

			float first = std::numeric_limits<float>::max();
			float last  = std::numeric_limits<float>::lowest();

			for (const tinygltf::AnimationChannel& channel : animation.channels)
			{
				if (channel.target_node < 0 ||
				    static_cast<size_t>(channel.target_node) >= model.nodes.size())
					continue;

				const auto node = static_cast<uint32_t>(channel.target_node);
				if (!inBoneChain[node])
					continue;  // a node no bone hangs off -- a camera, a prop, a morph target

				const auto samplerIndex = static_cast<uint32_t>(channel.sampler);
				if (channel.sampler < 0 ||
				    static_cast<size_t>(channel.sampler) >= animation.samplers.size())
					continue;

				if (!samplers.contains(samplerIndex))
				{
					const tinygltf::AnimationSampler& source =
						animation.samplers[static_cast<size_t>(channel.sampler)];

					KeyframeSampler sampler;
					int             timeComponents = 0;
					sampler.times  = readFloatAccessor(model, source.input, timeComponents);
					sampler.values = readFloatAccessor(model, source.output, sampler.components);
					sampler.interpolation = toInterpolation(source.interpolation);

					if (sampler.times.empty() || sampler.values.empty())
						continue;

					// evaluate returns a vec4 and fills one lane per component, so a MAT3 or MAT4
					// accessor here would run off the end of it.
					if (sampler.components < 1 || sampler.components > 4)
						throw_runtime_error(
							"banim: sampler output has {} components, which is not a TRS channel",
							sampler.components);

					// Times and values come from independent accessors, so nothing but this ties the
					// two together -- and evaluate indexes values by key without rechecking.
					const size_t keyStride =
						sampler.interpolation == Interpolation::kCubicSpline ? 3 : 1;
					if (sampler.values.size() <
					    sampler.times.size() * keyStride * static_cast<size_t>(sampler.components))
						throw_runtime_error(
							"banim: sampler has {} keyframe times but only {} values to match them",
							sampler.times.size(),
							sampler.values.size());

					first = std::min(first, sampler.times.front());
					last  = std::max(last, sampler.times.back());
					samplers.emplace(samplerIndex, std::move(sampler));
				}

				if (channel.target_path == "translation")
					channels[node].translationSampler = samplerIndex;
				else if (channel.target_path == "rotation")
					channels[node].rotationSampler = samplerIndex;
				else if (channel.target_path == "scale")
					channels[node].scaleSampler = samplerIndex;
				// "weights" drives morph targets, which this pipeline has no representation for.
			}

			if (samplers.empty())
				continue;  // nothing in this animation moves the rig, so it is not a clip of it

			const float duration   = std::max(0.0f, last - first);
			const auto  frameCount = frameCountFor(duration, sampleRate);

			AnimationClip clip{};
			clip.nameOffset  = out.stringPool.add(animation.name);
			clip.firstSample = static_cast<uint32_t>(out.samples.size());
			clip.frameCount  = frameCount;
			clip.sampleRate  = sampleRate;
			clip.duration    = duration;

			out.samples.resize(out.samples.size() + static_cast<size_t>(frameCount) * boneCount);

			// Only the nodes this clip animates are re-read per frame; the rest hold the pose they
			// were authored at, which is what a bone with no channel at all must come out as.
			auto nodePoses = authoredPoses;

			for (uint32_t f = 0; f < frameCount; ++f)
			{
				// The last frame lands on `duration` exactly, so a clip always covers its whole range
				// however the rate divides it.
				const float time = first + std::min(static_cast<float>(f) / sampleRate, duration);

				for (const auto& [node, driven] : channels)
				{
					Transform& pose = nodePoses[node];

					if (driven.translationSampler != c_InvalidIndex)
					{
						const auto value = evaluate(samplers.at(driven.translationSampler), time);
						pose.translation = glm::vec3(value);
					}
					if (driven.rotationSampler != c_InvalidIndex)
					{
						const auto value = evaluate(samplers.at(driven.rotationSampler), time);
						pose.rotation    = glm::quat(value.w, value.x, value.y, value.z);
					}
					if (driven.scaleSampler != c_InvalidIndex)
					{
						const auto value = evaluate(samplers.at(driven.scaleSampler), time);
						pose.scale       = glm::vec3(value);
					}
				}

				for (uint32_t b = 0; b < boneCount; ++b)
					out.samples[clip.firstSample + static_cast<size_t>(f) * boneCount + b] =
						composeBoneTransform(skin.boneNodes[b], nodePoses);
			}

			const std::span<const Transform> firstPose(
				out.samples.data() + clip.firstSample,
				boneCount);
			const std::span<const Transform> lastPose(
				out.samples.data() + clip.firstSample +
					static_cast<size_t>(frameCount - 1) * boneCount,
				boneCount);

			clip.rootMotion      = lastPose[0].translation - firstPose[0].translation;
			clip.locomotionSpeed = locomotionSpeedFor(clip.rootMotion, duration);

			// glTF has no loop flag, so the only evidence is the data: a clip authored to loop ends on
			// the pose it started from. A single-frame clip is a pose, not a loop.
			clip.loop = frameCount > 1 && posesMatch(firstPose, lastPose) ? 1u : 0u;

			out.clips.push_back(clip);
		}

		return out;
	}
}
