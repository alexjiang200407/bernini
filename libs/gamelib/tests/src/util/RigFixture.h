#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib/image_io.h>
#include <assetlib/skeleton.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>

// A rig on disk, as an importer would leave one: the .bmesh with its skin binding, the .bskel it
// names, a .banim cooked against that skeleton, and the material and texture the submesh needs.
// Shared by the VAT and skinned acquire suites, which need the same inputs and differ only in which
// door of the AssetManager they take them through.

namespace game::test
{
	namespace fs = std::filesystem;

	struct DataRoot
	{
		fs::path path;

		explicit DataRoot(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path);
		}
		~DataRoot() { fs::remove_all(path); }
	};

	// A 1x1 white .ktx2 so the material has something real to sample.
	inline void
	WriteTexture(const fs::path& path)
	{
		auto image      = assetlib::ImageData();
		image.width     = 1;
		image.height    = 1;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;

		image.pixels = core::fixed_buffer<std::byte>(4);
		std::fill_n(image.pixels.data(), 4, std::byte{ 0xFF });
		image.subresources.push_back({ 0, 4, 4 });

		fs::create_directories(path.parent_path());
		assetlib::writeKTX2(image, path, false, assetlib::Ktx2Compression::kNone);
	}

	/**
	 * The rig's material. `loose` writes it the way a material whose bake has gone stale is written:
	 * a routed base colour with no stamp against it and no baked triplet, which is what
	 * assetlib::drawsLoose reads as "sample the routes". Neither animated pipeline has a loose
	 * variant, so that is the shape both refuse.
	 */
	inline void
	WriteMaterial(const fs::path& path, bool loose)
	{
		auto material = assetlib::BMaterial();

		if (loose)
			material.pbr.routes[0].texture = "Textures/white.ktx2";
		else
			material.pbr.baseColorTexture = "Textures/white.ktx2";

		fs::create_directories(path.parent_path());
		assetlib::AssetStore(path.parent_path()).Save(material, path.filename().generic_string());
	}

	/**
	 * The rig on disk: one bone, a 4-vertex quad welded to it (one meshlet), and a 2-frame "slide"
	 * clip translating the bone +1 X per frame -- so the pose at frame f is the quad on
	 * [f - 1, f + 1], readable off the screen. Writes Meshes/rig.bmesh, Skeletons/rig.bskel,
	 * Animations/rig.banim and the material; deliberately NO .bvat -- producing one is the
	 * manager's job.
	 */
	inline void
	WriteRig(const fs::path& dataRoot, bool looseMaterial = false)
	{
		auto skeleton = assetlib::Skeleton();

		auto root       = assetlib::Bone();
		root.bindPose   = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		root.parent     = assetlib::c_InvalidIndex;
		root.nameOffset = skeleton.stringPool.add("root");
		skeleton.bones.push_back(root);

		const auto binds              = assetlib::bindPoseModelTransforms(skeleton);
		skeleton.bones[0].inverseBind = glm::inverse(binds[0]);

		auto animations              = assetlib::AnimationSet();
		animations.skeleton          = "Skeletons/rig.bskel";
		animations.skeletonSignature = assetlib::skeletonSignature(skeleton);
		animations.boneCount         = 1;

		auto slide        = assetlib::AnimationClip();
		slide.nameOffset  = animations.stringPool.add("slide");
		slide.firstSample = 0;
		slide.frameCount  = 2;
		slide.sampleRate  = 30.0f;
		slide.duration    = 1.0f / 30.0f;
		animations.clips.push_back(slide);

		for (uint32_t frame = 0; frame < 2; ++frame)
		{
			animations.samples.push_back(
				{ glm::vec3(static_cast<float>(frame), 0.0f, 0.0f),
			      glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			      glm::vec3(1.0f) });
		}

		auto mesh = assetlib::BMesh();

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 4;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              24 };
		submesh.layout.attributes[3]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              32 };
		submesh.layout.stride         = 40;

		// Corner order: bottom-left, bottom-right, top-left, top-right (CCW triangles below).
		const std::array<glm::vec3, 4> corners = { {
			{ -1.0f, -1.0f, 0.0f },
			{ 1.0f, -1.0f, 0.0f },
			{ -1.0f, 1.0f, 0.0f },
			{ 1.0f, 1.0f, 0.0f },
		} };

		for (const glm::vec3& corner : corners)
		{
			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			const glm::vec3               normal  = { 0.0f, 0.0f, 1.0f };
			const std::array<uint16_t, 4> joints  = { { 0, 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { 65535, 0, 0, 0 } };

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &corner, sizeof(corner));
			std::memcpy(at + 12, &normal, sizeof(normal));
			std::memcpy(at + 24, joints.data(), sizeof(joints));
			std::memcpy(at + 32, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingCenter = glm::vec3(0.0f);
		meshlet.boundingRadius = 2.0f;
		mesh.meshlets.push_back(meshlet);

		for (const uint32_t v : { 0u, 1u, 2u, 3u }) mesh.meshletVertices.push_back(v);
		for (const uint8_t t : { uint8_t(0),
		                         uint8_t(1),
		                         uint8_t(2),  // tri 0
		                         uint8_t(2),
		                         uint8_t(1),
		                         uint8_t(3) })
			mesh.meshletTriangles.push_back(t);

		submesh.firstMeshlet = 0;
		submesh.meshletCount = 1;
		submesh.material     = 0;
		submesh.aabbMin      = glm::vec3(-1.0f);
		submesh.aabbMax      = glm::vec3(1.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		mesh.materials.push_back("Materials/skin.bmaterial");
		mesh.skeleton = "Skeletons/rig.bskel";

		fs::create_directories(dataRoot / "Meshes");
		fs::create_directories(dataRoot / "Skeletons");
		fs::create_directories(dataRoot / "Animations");
		const assetlib::AssetStore store(dataRoot);
		store.Save(mesh, "Meshes/rig.bmesh");
		store.Save(skeleton, "Skeletons/rig.bskel");
		store.Save(animations, "Animations/rig.banim");

		WriteTexture(dataRoot / "Textures/white.ktx2");
		WriteMaterial(dataRoot / "Materials/skin.bmaterial", looseMaterial);
	}

	/**
	 * A clip set for the same rig, written to `banimRel`: one clip `name` of `frameCount` frames,
	 * the bone at `frame * strideX` -- so frame f puts the quad on [f * strideX - 1, f * strideX + 1].
	 */
	inline void
	WriteClips(
		const fs::path&  dataRoot,
		const fs::path&  banimRel,
		std::string_view name,
		float            strideX,
		uint32_t         frameCount)
	{
		const auto skeleton =
			assetlib::AssetStore(dataRoot).Load<assetlib::Skeleton>("Skeletons/rig.bskel");

		auto animations              = assetlib::AnimationSet();
		animations.skeleton          = "Skeletons/rig.bskel";
		animations.skeletonSignature = assetlib::skeletonSignature(skeleton);
		animations.boneCount         = 1;

		auto clip        = assetlib::AnimationClip();
		clip.nameOffset  = animations.stringPool.add(name);
		clip.firstSample = 0;
		clip.frameCount  = frameCount;
		clip.sampleRate  = 30.0f;
		clip.duration    = static_cast<float>(frameCount - 1) / 30.0f;
		animations.clips.push_back(clip);

		for (uint32_t frame = 0; frame < frameCount; ++frame)
		{
			animations.samples.push_back(
				{ glm::vec3(static_cast<float>(frame) * strideX, 0.0f, 0.0f),
			      glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			      glm::vec3(1.0f) });
		}

		assetlib::AssetStore(dataRoot).Save(animations, banimRel.generic_string());
	}
}
