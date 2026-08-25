#pragma once
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include <assetlib/skinning.h>

namespace assetlib::test
{
	constexpr uint16_t c_Unorm16Max = std::numeric_limits<uint16_t>::max();

	/**
	 * A two-bone rig (child hanging +2Y off the root), a mesh with one skinned and one static
	 * submesh, and two clips: "slide", whose child bone translates +X by one unit per frame, and
	 * "rest", one frame at the bind pose. Every expected position is closed-form.
	 */
	struct VatFixture
	{
		Skeleton     skeleton;
		AnimationSet animations;
		BMesh        mesh;

		VatFixture()
		{
			Bone root{};
			root.bindPose = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			root.parent   = c_InvalidIndex;
			root.nameOffset = skeleton.stringPool.add("root");
			skeleton.bones.push_back(root);

			Bone child{};
			child.bindPose   = { glm::vec3(0.0f, 2.0f, 0.0f),
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			child.parent     = 0;
			child.nameOffset = skeleton.stringPool.add("child");
			skeleton.bones.push_back(child);

			const auto binds = bindPoseModelTransforms(skeleton);
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
				skeleton.bones[i].inverseBind = glm::inverse(binds[i]);

			animations.skeleton          = "Skeletons/rig.bskel";
			animations.skeletonSignature = skeletonSignature(skeleton);
			animations.boneCount         = 2;

			AnimationClip slide{};
			slide.nameOffset  = animations.stringPool.add("slide");
			slide.firstSample = 0;
			slide.frameCount  = 3;
			slide.sampleRate  = 30.0f;
			slide.duration    = 2.0f / 30.0f;
			animations.clips.push_back(slide);

			for (uint32_t frame = 0; frame < 3; ++frame)
			{
				animations.samples.push_back(skeleton.bones[0].bindPose);
				animations.samples.push_back(
					{ glm::vec3(static_cast<float>(frame), 2.0f, 0.0f),
				      glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				      glm::vec3(1.0f) });
			}

			AnimationClip rest{};
			rest.nameOffset  = animations.stringPool.add("rest");
			rest.firstSample = 6;
			rest.frameCount  = 1;
			rest.sampleRate  = 30.0f;
			animations.clips.push_back(rest);

			animations.samples.push_back(skeleton.bones[0].bindPose);
			animations.samples.push_back(skeleton.bones[1].bindPose);

			// Submesh 0: two skinned vertices, one welded to each bone.
			Submesh skinned{};
			skinned.layout.attributeCount = 4;
			skinned.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			skinned.layout.attributes[1]  = { VertexSemantic::kNormal,
				                              VertexFormat::kFloat32x3,
				                              12 };
			skinned.layout.attributes[2]  = { VertexSemantic::kJoints0,
				                              VertexFormat::kUint16x4,
				                              24 };
			skinned.layout.attributes[3]  = { VertexSemantic::kWeights0,
				                              VertexFormat::kUnorm16x4,
				                              32 };
			skinned.layout.stride         = 40;

			AddSkinnedVertex(skinned, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0);
			AddSkinnedVertex(skinned, glm::vec3(0.0f, 2.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 1);
			mesh.submeshes.push_back(skinned);

			// Submesh 1: one static vertex -- a saddle, which every frame must leave alone.
			Submesh fixture{};
			fixture.layout.attributeCount = 1;
			fixture.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			fixture.layout.stride         = 12;
			fixture.vertexByteOffset      = static_cast<uint32_t>(mesh.vertexData.size());

			const glm::vec3 saddle(5.0f, 5.0f, 5.0f);
			mesh.vertexData.resize(mesh.vertexData.size() + 12);
			std::memcpy(mesh.vertexData.data() + fixture.vertexByteOffset, &saddle, sizeof(saddle));
			fixture.vertexCount = 1;
			mesh.submeshes.push_back(fixture);

			mesh.skeleton = "Skeletons/rig.bskel";
		}

		void
		AddSkinnedVertex(
			Submesh&         submesh,
			const glm::vec3& position,
			const glm::vec3& normal,
			uint16_t         bone)
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const std::array<uint16_t, 4> joints  = { { bone, 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { c_Unorm16Max, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, &normal, sizeof(normal));
			std::memcpy(at + 24, joints.data(), sizeof(joints));
			std::memcpy(at + 32, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}
	};
}
