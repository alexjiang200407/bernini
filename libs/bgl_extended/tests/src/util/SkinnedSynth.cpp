#include "util/SkinnedSynth.h"

#include "util/VertexPacking.h"

#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

namespace bgl::test::skinned_synth
{
	const std::array<glm::vec3, 4> c_QuadAtOrigin = { {
		{ -1.0f, -1.0f, 0.0f },
		{ 1.0f, -1.0f, 0.0f },
		{ -1.0f, 1.0f, 0.0f },
		{ 1.0f, 1.0f, 0.0f },
	} };

	namespace
	{
		// Wide enough for both poses, so culling never depends on which frame is showing.
		const auto c_PosedBounds =
			assetlib::Bounds{ glm::vec3(-1.5f, -1.5f, -1.0f), glm::vec3(2.5f, 1.5f, 1.0f) };

		assetlib::Skeleton
		MakeOneBoneRig()
		{
			auto skeleton = assetlib::Skeleton();

			auto bone     = assetlib::Bone();
			bone.bindPose = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
			bone.inverseBind = glm::mat4(1.0f);
			bone.parent      = assetlib::c_InvalidIndex;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);

			return skeleton;
		}

		assetlib::Transform
		SlidTo(float x)
		{
			auto sample        = assetlib::Transform();
			sample.translation = glm::vec3(x, 0.0f, 0.0f);
			sample.rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			sample.scale       = glm::vec3(1.0f);
			return sample;
		}

		assetlib::AnimationClip
		Clip(uint32_t firstSample, uint32_t frameCount, uint32_t loop)
		{
			auto clip            = assetlib::AnimationClip();
			clip.nameOffset      = 0;
			clip.firstSample     = firstSample;
			clip.frameCount      = frameCount;
			clip.sampleRate      = c_SampleRate;
			clip.duration        = static_cast<float>(frameCount - 1) / c_SampleRate;
			clip.rootMotion      = glm::vec3(0.0f);
			clip.locomotionSpeed = 0.0f;
			clip.loop            = loop;
			return clip;
		}

		assetlib::AnimationSet
		MakeSlideClips()
		{
			auto set      = assetlib::AnimationSet();
			set.boneCount = 1;

			// One bone, so a sample index is a frame index and the two clips simply follow one
			// another in the pool.
			set.samples = { SlidTo(0.0f),
				            SlidTo(c_Step),
				            SlidTo(0.0f),  // the loop's duplicate of its own frame 0
				            SlidTo(0.0f),
				            SlidTo(c_Step) };

			set.clips.push_back(Clip(0, 3, 1));
			set.clips.push_back(Clip(3, 2, 0));

			return set;
		}

		assetlib::BMesh
		MakeQuad()
		{
			auto mesh = assetlib::BMesh();
			mesh.vertexData.assign(size_t(4) * c_SkinnedVertexStride, std::byte{ 0 });

			for (uint32_t v = 0; v < 4; ++v)
			{
				const size_t     base   = size_t(v) * c_SkinnedVertexStride;
				const glm::vec3& corner = c_QuadAtOrigin[v];

				const std::array<float, 3> pos    = { { corner.x, corner.y, corner.z } };
				const std::array<float, 3> normal = { { 0.0f, 0.0f, 1.0f } };
				const std::array<float, 2> uv     = { { corner.x * 0.5f + 0.5f,
					                                    corner.y * 0.5f + 0.5f } };
				const std::array<float, 4> tan    = { { 1.0f, 0.0f, 0.0f, 1.0f } };

				PutFloats(mesh.vertexData, base + 0, pos);
				PutFloats(mesh.vertexData, base + 12, normal);
				PutFloats(mesh.vertexData, base + 24, uv);
				PutFloats(mesh.vertexData, base + 32, tan);

				// Every vertex on the one bone at full weight -- unorm16 0xFFFF is exactly 1.0 -- so
				// the whole quad is the bone's pose and nothing here blends.
				const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
				const std::array<uint16_t, 4> weights = { { 0xFFFF, 0, 0, 0 } };
				PutU16x4(mesh.vertexData, base + 48, joints);
				PutU16x4(mesh.vertexData, base + 56, weights);
			}

			auto meshlet           = assetlib::Meshlet();
			meshlet.vertexCount    = 4;
			meshlet.triangleCount  = 2;
			meshlet.boundingCenter = glm::vec3(0.5f, 0.0f, 0.0f);
			meshlet.boundingRadius = 2.5f;
			mesh.meshlets.push_back(meshlet);

			for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);

			// Meshlet-local indices, and uint8_t rather than a bare braced list: an int list narrows
			// on the way in, which MSVC refuses under the project's warning-as-error settings.
			const std::array<uint8_t, 6> tris = { { 0, 1, 2, 2, 1, 3 } };
			for (const uint8_t t : tris) mesh.meshletTriangles.push_back(t);

			auto submesh         = assetlib::Submesh();
			submesh.layout       = SkinnedVertexLayout();
			submesh.vertexCount  = 4;
			submesh.meshletCount = 1;
			submesh.material     = 0;

			submesh.aabbMin = glm::vec3(-1.0f, -1.0f, 0.0f);
			submesh.aabbMax = glm::vec3(1.0f, 1.0f, 0.0f);
			mesh.submeshes.push_back(submesh);

			auto entry         = assetlib::Mesh();
			entry.submeshCount = 1;
			mesh.meshes.push_back(entry);

			return mesh;
		}
	}

	GeomHandle
	AddSlidingQuadGeom(IScene& scene, MaterialHandle material)
	{
		const std::array<MaterialHandle, 1> materials = { { material } };

		return scene.AddSkinnedMeshGeom(
			MakeQuad(),
			0,
			materials,
			scene.AddRig(MakeOneBoneRig(), MakeSlideClips()),
			c_PosedBounds);
	}
}
