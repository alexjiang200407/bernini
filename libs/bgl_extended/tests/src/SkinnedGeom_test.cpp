#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <assetlib_structs/Bounds.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// What AddSkinnedMeshGeom uploads, and what it refuses. Nothing here draws: the skinned forward
// kernel does not exist yet, so a skinned submesh resolves to PsoType::kInvalid and the counting
// sort skips it. The tables and the playback record are the whole of what this task delivers, so
// they are read back off the CPU mirror -- which is exactly the bytes Update() uploads.

namespace
{
	// position (float32x3) + joints0 (uint16x4) + weights0 (unorm16x4).
	constexpr uint16_t c_SkinnedStride = 12 + 8 + 8;

	constexpr uint32_t c_BoneCount = 3;
	constexpr uint32_t c_Frames    = 3;  // clip 0 takes two, clip 1 the third

	// Nothing here draws or culls, so any well-formed box does. The cases that are *about* the box
	// build their own.
	const auto c_AnyPose =
		assetlib::Bounds{ glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f) };

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	TestSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialSubmeshes            = 8;
		desc.initialMeshlets             = 8;
		desc.initialVertexBufferByteSize = 4096;
		desc.initialIndices              = 64;
		desc.initialPbrMaterials         = 4;
		return desc;
	}

	/**
	 * One triangle, one submesh, carrying skin binding. `withSkin == false` drops joints0/weights0
	 * to exercise the refusal -- the stride shrinks with them, so the vertex data stays consistent.
	 */
	assetlib::BMesh
	MakeSkinnedMesh(bool withSkin = true)
	{
		const uint16_t stride = withSkin ? c_SkinnedStride : 12;

		auto mesh = assetlib::BMesh();
		mesh.vertexData.resize(size_t(3) * stride);

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingCenter = glm::vec3(0.0f);
		meshlet.boundingRadius = 1.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
		for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = withSkin ? 3 : 1;
		submesh.layout.stride         = stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		if (withSkin)
		{
			submesh.layout.attributes[1] = { assetlib::VertexSemantic::kJoints0,
				                             assetlib::VertexFormat::kUint16x4,
				                             12 };
			submesh.layout.attributes[2] = { assetlib::VertexSemantic::kWeights0,
				                             assetlib::VertexFormat::kUnorm16x4,
				                             20 };
		}
		submesh.vertexByteOffset = 0;
		submesh.vertexCount      = 3;
		submesh.firstMeshlet     = 0;
		submesh.meshletCount     = 1;
		submesh.material         = 0;
		submesh.aabbMin          = glm::vec3(-1.0f);
		submesh.aabbMax          = glm::vec3(1.0f);
		submesh.nameOffset       = 0;
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 1;
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	/**
	 * A chain: bone 0 is the root, 1 its child, 2 its grandchild -- so depth is 0, 1, 2 and every
	 * bone has a distinguishable inverse bind. A chain, not a star, because a star would pass a walk
	 * that ignored `parent` entirely.
	 */
	assetlib::Skeleton
	MakeRig(uint32_t boneCount = c_BoneCount)
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < boneCount; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = { glm::vec3(0.0f, float(i), 0.0f),
				                 glm::quat(1, 0, 0, 0),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/**
	 * Two clips over one pool of `c_Frames` frames: clip 0 loops over the first two, clip 1 holds the
	 * third. Every sample is distinguishable by frame and bone, so a wrong stride shows up as a wrong
	 * value rather than as a plausible one.
	 */
	assetlib::AnimationSet
	MakeClips(uint32_t boneCount = c_BoneCount)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = boneCount;

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < boneCount; ++b)
			{
				auto sample        = assetlib::Transform();
				sample.translation = glm::vec3(float(f), float(b), float(f * boneCount + b));
				sample.rotation    = glm::quat(1.0f, 0.0f, 0.0f, float(f) * 0.25f);
				sample.scale       = glm::vec3(1.0f + float(b) * 0.5f);
				set.samples.push_back(sample);
			}
		}

		auto looping        = assetlib::AnimationClip();
		looping.firstSample = 0;
		looping.frameCount  = 2;
		looping.sampleRate  = 30.0f;
		looping.duration    = 1.0f / 30.0f;
		looping.loop        = 1;
		looping.nameOffset  = 0;
		set.clips.push_back(looping);

		auto held        = assetlib::AnimationClip();
		held.firstSample = 2 * boneCount;
		held.frameCount  = 1;
		held.sampleRate  = 60.0f;
		held.duration    = 0.0f;
		held.loop        = 0;
		held.nameOffset  = 0;
		set.clips.push_back(held);

		return set;
	}

	bgl::MaterialHandle
	OpaquePbr(bgl::Scene* scene)
	{
		auto desc = bgl::PbrMaterialDesc();
		return scene->CreatePbrMaterial(desc);
	}
}

TEST_CASE("AddSkinnedMeshGeom uploads a rig's bones, clips and samples", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const auto material = OpaquePbr(scene);
	REQUIRE(material.IsValid());

	const auto                               skeleton   = MakeRig();
	const auto                               animations = MakeClips();
	const std::array<bgl::MaterialHandle, 1> materials  = { { material } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedMesh(),
		0,
		materials,
		scene->AddRig(skeleton, animations),
		c_AnyPose);
	REQUIRE(geom.IsValid());
	REQUIRE(geom.geomType == bgl::GeomType::kSkinnedMesh);

	auto& clips   = scene->GetClipBuffer();
	auto& rigs    = scene->GetRigBuffer();
	auto& bones   = scene->GetSkinnedBoneBuffer();
	auto& samples = scene->GetBoneSampleBuffer();

	const bgl::Scene::AnimGeomInfo info = scene->GetGeomSkinnedInfo(geom.handle.index);
	REQUIRE(info.record);
	REQUIRE(info.clipCount == 2);

	const bgl::idl::Rig& record = rigs[info.record];
	CHECK(record.boneCount == c_BoneCount);
	CHECK(record.clips.count == 2);

	SECTION("depth is derived from parent, and maxDepth is the deepest")
	{
		// A chain of three: the walk needs three levels, so the deepest depth is 2.
		CHECK(record.maxDepth == c_BoneCount - 1);

		const uint32_t boneRoot = record.bones.offsetStart;
		for (uint32_t i = 0; i < c_BoneCount; ++i)
		{
			const bgl::idl::SkinnedBone& bone = bones.AtIndex(boneRoot + i);
			CHECK(bone.depth == i);
			CHECK(bone.parent == (i == 0 ? bgl::idl::cInvalidBone : i - 1));
			CHECK(bone.inverseBind[3][1] == Catch::Approx(-float(i)));
		}
	}

	SECTION("the clip table carries firstSample, frameCount, rate and the loop flag")
	{
		const uint32_t clipRoot = record.clips.range.offsetStart;

		const bgl::idl::Clip& looping = clips.AtIndex(clipRoot + 0);
		CHECK(looping.firstFrame == 0);
		CHECK(looping.frameCount == 2);
		CHECK(looping.sampleRate == Catch::Approx(30.0f));
		CHECK(looping.loop == 1);

		// The pool is frame-major and idl::Clip addresses frames, so the second clip's base is a
		// frame index -- 2 -- not the sample index (2 * boneCount) assetlib stores.
		const bgl::idl::Clip& held = clips.AtIndex(clipRoot + 1);
		CHECK(held.firstFrame == 2);
		CHECK(held.frameCount == 1);
		CHECK(held.sampleRate == Catch::Approx(60.0f));
		CHECK(held.loop == 0);
	}

	SECTION("samples round-trip frame-major, with the quaternion's w kept")
	{
		const uint32_t sampleRoot = record.samples.offsetStart;
		REQUIRE(animations.samples.size() == size_t(c_Frames) * c_BoneCount);

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_BoneCount; ++b)
			{
				const uint32_t              i   = f * c_BoneCount + b;
				const assetlib::Transform&  src = animations.samples[i];
				const bgl::idl::BoneSample& out = samples.AtIndex(sampleRoot + i);

				CHECK(out.translation.x == Catch::Approx(src.translation.x));
				CHECK(out.translation.y == Catch::Approx(src.translation.y));
				CHECK(out.translation.z == Catch::Approx(src.translation.z));

				// glm::quat stores w first; the GPU sample is xyzw. Getting this backwards is
				// invisible on an identity rotation, which is why the fixture's w varies by frame.
				CHECK(out.rotation.x == Catch::Approx(src.rotation.x));
				CHECK(out.rotation.y == Catch::Approx(src.rotation.y));
				CHECK(out.rotation.z == Catch::Approx(src.rotation.z));
				CHECK(out.rotation.w == Catch::Approx(src.rotation.w));

				CHECK(out.scale.x == Catch::Approx(src.scale.x));
				CHECK(out.scale.y == Catch::Approx(src.scale.y));
				CHECK(out.scale.z == Catch::Approx(src.scale.z));
			}
		}
	}

	SECTION("a skinned geom's material can be rebound to any layer of kPBR, but not to loose")
	{
		auto rebound           = bgl::PbrMaterialDesc();
		rebound.metallicFactor = 0.25f;
		CHECK_NOTHROW(scene->SetSubmeshMaterial(geom, 0, scene->CreatePbrMaterial(rebound)));

		for (const bgl::LayerType layer :
		     { bgl::LayerType::kMask, bgl::LayerType::kBlend, bgl::LayerType::kHashed })
		{
			auto layerDesc      = bgl::PbrMaterialDesc();
			layerDesc.layerType = layer;
			CHECK_NOTHROW(scene->SetSubmeshMaterial(geom, 0, scene->CreatePbrMaterial(layerDesc)));
		}

		// There is no loose variant of the skinned geometry stage, so AcceptsMaterial refuses the
		// handle here rather than letting a draw reach a pixel shader that cannot route channels.
		const auto loose = scene->CreateLoosePbrMaterial(bgl::LoosePbrMaterialDesc());
		CHECK_THROWS_AS(scene->SetSubmeshMaterial(geom, 0, loose), bgl::SceneError);
	}
}

TEST_CASE("CreateSkinnedMeshInstance writes the playback record once", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto  viewHandle = gfx->CreateSceneView(sceneHandle, 8);
	auto* view       = viewHandle->As<bgl::SceneView>();
	REQUIRE(view != nullptr);

	const std::array<bgl::MaterialHandle, 1> materials = { { OpaquePbr(scene) } };
	const auto                               geom      = scene->AddSkinnedMeshGeom(
		MakeSkinnedMesh(),
		0,
		materials,
		scene->AddRig(MakeRig(), MakeClips()),
		c_AnyPose);
	REQUIRE(geom.IsValid());

	auto desc  = bgl::SkinnedInstanceDesc();
	desc.clip  = 1;
	desc.phase = 4.5f;
	desc.rate  = 2.0f;

	const auto placed = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

	const auto instance = view->CreateSkinnedMeshInstance(geom, placed, desc);
	REQUIRE(instance.IsValid());

	auto& meshBuffer = view->GetMeshBuffer();
	auto& playback   = view->GetPlaybackArena();

	const bgl::idl::MeshInstance& mesh = meshBuffer.AtIndex(instance.handle.index);

	// One field for either tier now, so what says which is the record's own header rather than
	// which of two fields was left null.
	REQUIRE_FALSE(mesh.playback.Null());
	CHECK(playback.GetTagAt(mesh.playback.byteOffset) == bgl::idl::PlaybackType::kSkinned);

	const auto state = playback.GetPayloadAt<bgl::idl::SkinnedState>(mesh.playback.byteOffset);
	CHECK(state.playback.clip == 1);
	CHECK(state.playback.phase == Catch::Approx(4.5f));
	CHECK(state.playback.rate == Catch::Approx(2.0f));
	CHECK(state.playback.rig.offset == scene->GetGeomSkinnedInfo(geom.handle.index).record.index);

	SECTION("a crowd instance is a record of its own kind, owning no palette")
	{
		auto crowd   = desc;
		crowd.source = bgl::PoseSource::kBoneAnimTable;

		const auto other = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), crowd);
		REQUIRE(other.IsValid());

		const bgl::idl::MeshInstance& crowdMesh = meshBuffer.AtIndex(other.handle.index);
		CHECK(
			playback.GetTagAt(crowdMesh.playback.byteOffset) ==
			bgl::idl::PlaybackType::kSkinnedTable);

		// The same playback, read out of a different record. What the crowd one does
		// not carry is the palette, which is the whole of the difference between the two kinds --
		// so the layouts differ, and a record read as the wrong kind would not survive this.
		const auto table =
			playback.GetPayloadAt<bgl::idl::SkinnedTableState>(crowdMesh.playback.byteOffset);
		CHECK(table.playback.clip == 1);
		CHECK(table.playback.phase == Catch::Approx(4.5f));
		CHECK(table.playback.rate == Catch::Approx(2.0f));
		CHECK(table.playback.rig.offset == state.playback.rig.offset);

		CHECK(sizeof(bgl::idl::SkinnedTableState) < sizeof(bgl::idl::SkinnedState));
	}

	// Where the instance stands is the placement's, and only the placement's: the pose pass reads
	// it from here rather than from a copy of its own.
	auto expected = bgl::idl::MeshInstance();
	bgl::WriteInstanceTransform(expected, placed);
	CHECK(mesh.transform[0] == expected.transform[0]);
	CHECK(mesh.transform[1] == expected.transform[1]);
	CHECK(mesh.transform[2] == expected.transform[2]);

	SECTION("a clip past the geom's table is refused")
	{
		auto tooFar = desc;
		tooFar.clip = 2;
		CHECK_THROWS_AS(
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), tooFar),
			bgl::SceneError);
	}

	SECTION("a static geom is refused")
	{
		const auto cube = scene->AddCubeGeom(bgl::MaterialHandle());
		CHECK_THROWS_AS(
			view->CreateSkinnedMeshInstance(cube, glm::mat4(1.0f), desc),
			bgl::SceneError);
	}

	SECTION("deleting the instance releases its state, and the geom its tables")
	{
		view->DeleteMeshInstance(instance);
		CHECK(view->GetInstanceCount() == 0);

		scene->DeleteGeom(geom);
		CHECK_FALSE(scene->IsGeomAlive(geom));
	}
}

TEST_CASE("AddRig refuses a rig the pose pass could not walk", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	SECTION("a skeleton with no bones")
	{
		CHECK_THROWS_AS(scene->AddRig(assetlib::Skeleton(), MakeClips(0)), bgl::SceneError);
	}

	SECTION("a parent that is not a lower index than its own bone")
	{
		auto skeleton            = MakeRig();
		skeleton.bones[1].parent = 2;  // forward reference: the walk would read it unwritten
		CHECK_THROWS_AS(scene->AddRig(skeleton, MakeClips()), bgl::SceneError);
	}

	SECTION("a clip set cooked against a different bone count")
	{
		auto animations      = MakeClips();
		animations.boneCount = c_BoneCount + 1;
		CHECK_THROWS_AS(scene->AddRig(MakeRig(), animations), bgl::SceneError);
	}

	SECTION("an empty clip table")
	{
		auto animations = MakeClips();
		animations.clips.clear();
		CHECK_THROWS_AS(scene->AddRig(MakeRig(), animations), bgl::SceneError);
	}

	SECTION("a clip with no frames")
	{
		auto animations                = MakeClips();
		animations.clips[0].frameCount = 0;
		CHECK_THROWS_AS(scene->AddRig(MakeRig(), animations), bgl::SceneError);
	}

	SECTION("a clip whose frames run past the sample pool")
	{
		auto animations                = MakeClips();
		animations.clips[1].frameCount = 4;
		CHECK_THROWS_AS(scene->AddRig(MakeRig(), animations), bgl::SceneError);
	}

	// Every refusal above must leave the scene addable: one that leaked a range would show up here
	// as a rig that no longer lands where a first rig lands.
	CHECK(scene->AddRig(MakeRig(), MakeClips()).IsValid());
}

TEST_CASE("AddSkinnedMeshGeom refuses a mesh the skinned path could not draw", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const std::array<bgl::MaterialHandle, 1> materials = { { OpaquePbr(scene) } };

	const bgl::RigHandle rig = scene->AddRig(MakeRig(), MakeClips());
	REQUIRE(rig.IsValid());

	SECTION("a submesh with no skin binding")
	{
		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(false), 0, materials, rig, c_AnyPose),
			bgl::SceneError);
	}

	SECTION("a submesh whose material is loose, which the skinned pipeline has no variant for")
	{
		const std::array<bgl::MaterialHandle, 1> loose = { { scene->CreateLoosePbrMaterial(
			bgl::LoosePbrMaterialDesc()) } };

		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, loose, rig, c_AnyPose),
			bgl::SceneError);
	}

	SECTION("but every layer of a kPBR one is uploaded")
	{
		for (const bgl::LayerType layer :
		     { bgl::LayerType::kMask, bgl::LayerType::kBlend, bgl::LayerType::kHashed })
		{
			auto layerDesc      = bgl::PbrMaterialDesc();
			layerDesc.layerType = layer;

			const std::array<bgl::MaterialHandle, 1> layered = { { scene->CreatePbrMaterial(
				layerDesc) } };

			const bgl::GeomHandle uploaded =
				scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, layered, rig, c_AnyPose);

			CHECK(uploaded.IsValid());
		}
	}

	SECTION("a meshIndex past the mesh table")
	{
		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 1, materials, rig, c_AnyPose),
			bgl::SceneError);
	}

	SECTION("a posed box whose min is past its max")
	{
		const auto inverted =
			assetlib::Bounds{ glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(-1.0f, 1.0f, 1.0f) };

		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, rig, inverted),
			bgl::SceneError);
	}

	SECTION("a rig that was never added, or was deleted")
	{
		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, bgl::RigHandle(), c_AnyPose),
			bgl::SceneError);

		const bgl::RigHandle retired = scene->AddRig(MakeRig(), MakeClips());
		scene->DeleteRig(retired);
		CHECK_THROWS_AS(
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, retired, c_AnyPose),
			bgl::SceneError);
	}

	// Every refusal above must leave the scene addable: a failed add that leaked its geometry half
	// would show up here as a geom slot or a submesh range that never came back.
	const auto good = scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, rig, c_AnyPose);
	CHECK(good.IsValid());
}

TEST_CASE("a refused skinned add leaves the scene's arenas untouched", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const std::array<bgl::MaterialHandle, 1> materials = { { OpaquePbr(scene) } };

	// Every range allocator hands back the lowest free block, so if a failed add left anything
	// behind, the *next* add lands somewhere else -- which is the only evidence of a leak that does
	// not need an occupancy accessor the buffers do not expose.
	const auto offsetsOfAFreshRig = [&] {
		const bgl::RigHandle rig = scene->AddRig(MakeRig(), MakeClips());
		REQUIRE(rig.IsValid());

		const auto& record = scene->GetRigBuffer()[rig.handle];
		const auto  taken  = std::array<uint32_t, 3>{
			{ record.bones.offsetStart, record.samples.offsetStart, record.clips.range.offsetStart }
		};
		scene->DeleteRig(rig);
		return taken;
	};

	const std::array<uint32_t, 3> beforeRig = offsetsOfAFreshRig();

	// A rig-shaped refusal is AddRig's, and it takes its bone and sample ranges before it can find
	// the fault -- so its rollback is what has to hold.
	auto badRig            = MakeRig();
	badRig.bones[1].parent = 2;
	CHECK_THROWS(scene->AddRig(badRig, MakeClips()));

	auto shortPool                = MakeClips();
	shortPool.clips[1].frameCount = 4;
	CHECK_THROWS(scene->AddRig(MakeRig(), shortPool));

	CHECK(offsetsOfAFreshRig() == beforeRig);

	// The mesh-shaped half, against a rig that stands throughout: those checks run after the mesh
	// has been cooked, so AddSkinnedMeshGeom's own rollback is what has to hold.
	const bgl::RigHandle rig = scene->AddRig(MakeRig(), MakeClips());
	REQUIRE(rig.IsValid());

	const auto offsetsOfAFreshGeom = [&] {
		const auto geom =
			scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, rig, c_AnyPose);
		REQUIRE(geom.IsValid());

		const uint32_t taken = scene->GetGeomSubmeshes(geom.handle.index).range.offsetStart;
		scene->DeleteGeom(geom);
		return taken;
	};

	const uint32_t beforeGeom = offsetsOfAFreshGeom();

	CHECK_THROWS(scene->AddSkinnedMeshGeom(MakeSkinnedMesh(false), 0, materials, rig, c_AnyPose));

	const std::array<bgl::MaterialHandle, 1> looseMaterials = { { scene->CreateLoosePbrMaterial(
		bgl::LoosePbrMaterialDesc()) } };
	CHECK_THROWS(scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, looseMaterials, rig, c_AnyPose));

	CHECK(offsetsOfAFreshGeom() == beforeGeom);

	// A refused add must not have counted a use either, or the rig could never be deleted.
	CHECK_NOTHROW(scene->DeleteRig(rig));
}

TEST_CASE("skinned geoms share one rig, and it outlives them", "[skinned]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const std::array<bgl::MaterialHandle, 1> materials = { { OpaquePbr(scene) } };

	const bgl::RigHandle rig = scene->AddRig(MakeRig(), MakeClips());
	REQUIRE(rig.IsValid());

	const auto first  = scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, rig, c_AnyPose);
	const auto second = scene->AddSkinnedMeshGeom(MakeSkinnedMesh(), 0, materials, rig, c_AnyPose);
	REQUIRE(first.IsValid());
	REQUIRE(second.IsValid());

	// The point of a rig being an object of its own: a unit assembled from slot meshes uploads one
	// bone table and one sample pool, not one per mesh.
	const auto firstInfo  = scene->GetGeomSkinnedInfo(first.handle.index);
	const auto secondInfo = scene->GetGeomSkinnedInfo(second.handle.index);
	CHECK(firstInfo.record == secondInfo.record);
	CHECK(firstInfo.boneCount == secondInfo.boneCount);
	CHECK(firstInfo.clipCount == secondInfo.clipCount);

	// Refused rather than permitted: a geom left naming freed bone and sample ranges would pose from
	// whatever lands in them next.
	CHECK_THROWS_AS(scene->DeleteRig(rig), bgl::SceneError);

	scene->DeleteGeom(first);
	CHECK_THROWS_AS(scene->DeleteRig(rig), bgl::SceneError);

	scene->DeleteGeom(second);
	CHECK_NOTHROW(scene->DeleteRig(rig));

	// And it is gone: a second delete has nothing to free.
	CHECK_THROWS_AS(scene->DeleteRig(rig), bgl::SceneError);
}

TEST_CASE("a skinned submesh culls by its posed box, not its bind pose", "[skinned][culling]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	const std::array<bgl::MaterialHandle, 1> materials = { { OpaquePbr(scene) } };

	// Off-center and far past the fixture's bind pose, which is the shape of the real failure: a clip
	// carrying root motion poses well outside the bind box, and culling by that box makes it vanish.
	const auto posed =
		assetlib::Bounds{ glm::vec3(-100.0f, 0.0f, -100.0f), glm::vec3(100.0f, 300.0f, 100.0f) };

	const auto skinned = scene->AddSkinnedMeshGeom(
		MakeSkinnedMesh(),
		0,
		materials,
		scene->AddRig(MakeRig(), MakeClips()),
		posed);
	REQUIRE(skinned.IsValid());

	// The same bytes as static geometry: its sphere is the cooked bind pose, so the two spheres
	// differing is the whole point -- and a skinned add that ignored its box would match it.
	const auto asStatic = scene->AddStaticMeshGeom(MakeSkinnedMesh(), 0, materials);
	REQUIRE(asStatic.IsValid());

	auto& submeshBuffer = scene->GetSubmeshBuffer();

	const glm::vec4 skinnedSphere =
		submeshBuffer.AtIndex(scene->GetGeomSubmeshes(skinned.handle.index).range.offsetStart)
			.boundingSphere;
	const glm::vec4 staticSphere =
		submeshBuffer.AtIndex(scene->GetGeomSubmeshes(asStatic.handle.index).range.offsetStart)
			.boundingSphere;

	const glm::vec3 center = (posed.min + posed.max) * 0.5f;
	CHECK(skinnedSphere.x == Catch::Approx(center.x));
	CHECK(skinnedSphere.y == Catch::Approx(center.y));
	CHECK(skinnedSphere.z == Catch::Approx(center.z));
	CHECK(skinnedSphere.w == Catch::Approx(glm::length(posed.max - posed.min) * 0.5f));

	CHECK(skinnedSphere.w > staticSphere.w);
}
