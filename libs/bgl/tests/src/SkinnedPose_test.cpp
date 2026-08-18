#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// What the pose pass computes, read straight off the GPU. A golden image can tell you a skinned pose
// is wrong; only this can tell you *which* stage got it wrong -- a bad inverse bind, a hierarchy walk
// that skipped a level, and a mis-strided sample fetch all look the same on screen.

namespace
{
	constexpr uint32_t c_Width      = 256;
	constexpr uint32_t c_Height     = 256;
	constexpr uint32_t c_BoneCount  = 3;
	constexpr float    c_SampleRate = 30.0f;

	// Frame 0 is the bind pose, frame 1 swings bone 1 by 90 degrees about +Z. Two frames is the
	// minimum that makes interpolation observable, and the rig is a chain so a walk that ignored
	// `parent` cannot pass.
	constexpr uint32_t c_Frames = 2;

	/**
	 * A three-bone chain standing up +Y: bone 0 at the origin, each child one unit above its parent.
	 * Every inverse bind is the exact inverse of that bind pose, so a pose *equal* to the bind pose
	 * must produce an identity palette -- which is the cheapest possible check that the inverse binds
	 * and the walk agree.
	 */
	assetlib::Skeleton
	MakeChain()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_BoneCount; ++i)
		{
			auto bone     = assetlib::Bone();
			bone.bindPose = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
				              glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				              glm::vec3(1.0f) };

			// Model-space bind of bone i is (0, i, 0), so its inverse takes a model point back by it.
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	assetlib::AnimationSet
	MakeSwingClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		// Rz(90) as xyzw = (0, 0, sin45, cos45); glm::quat takes w first.
		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_BoneCount; ++b)
			{
				auto sample        = assetlib::Transform();
				sample.translation = glm::vec3(0.0f, b == 0 ? 0.0f : 1.0f, 0.0f);
				sample.rotation    = (f == 1 && b == 1) ? swing : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale       = glm::vec3(1.0f);
				set.samples.push_back(sample);
			}
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = c_Frames;
		clip.sampleRate  = c_SampleRate;
		clip.duration    = 1.0f / c_SampleRate;
		clip.loop        = 0;  // one-shot, so a held frame is exactly the frame asked for
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/** One triangle carrying skin binding; the geometry is irrelevant, the rig is the subject. */
	assetlib::BMesh
	MakeSkinnedTriangle()
	{
		constexpr uint16_t c_Stride = 12 + 8 + 8;

		auto mesh = assetlib::BMesh();
		mesh.vertexData.resize(size_t(3) * c_Stride);

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingRadius = 4.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
		for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 3;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              20 };
		submesh.vertexCount           = 3;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-1.0f);
		submesh.aabbMax               = glm::vec3(3.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	/**
	 * The palette as the pose pass left it: `cFloat4sPerBone` rows a bone, each row of a skinning
	 * matrix as the shader indexed it, so `dot(row[i], float4(p, 1))` is component i of `p` skinned.
	 */
	struct Palette
	{
		std::vector<glm::vec4> rows;

		[[nodiscard]] glm::vec3
		Apply(uint32_t bone, const glm::vec3& point) const
		{
			const glm::vec4 p(point, 1.0f);
			const size_t    base = size_t(bone) * bgl::idl::cFloat4sPerBone;
			return { glm::dot(rows[base + 0], p),
				     glm::dot(rows[base + 1], p),
				     glm::dot(rows[base + 2], p) };
		}
	};

	/** Copies `float4Count` float4s out of the view's palette arena, starting at `base`. */
	Palette
	ReadPalette(
		bgl::GraphicsBase*    gfxBase,
		const bgl::SceneView* view,
		uint32_t              base,
		uint32_t              float4Count)
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();

		auto cmdListDesc = bgl::CommandListDesc();
		cmdListDesc.type = bgl::QueueType::kGraphics;

		// Drain the renderer first: this copy rides its own queue, which nothing orders against the
		// frame that wrote the palette.
		gfxBase->WaitIdle();

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();

		const bgl::BufferHandle palettes = view->GetPalettes().GetBufferHandle();

		// The whole arena, not just the slice wanted: CopyBufferToReadback copies the entire source
		// buffer, so a destination sized to the slice overruns it -- silently, until GPU validation
		// is on.
		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = uint64_t(view->GetPalettes().Capacity()) * sizeof(glm::vec4);
		rbDesc.debugName = "Bone Palette Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);

		// The pass left it in UAV state; a copy needs it as a source.
		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(palettes, barrier);

		cmdList->CopyBufferToReadback(rb, palettes);
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);

		auto palette = Palette();
		palette.rows.assign(mapped + base, mapped + base + float4Count);

		resourceManager->UnmapReadback(rb);
		return palette;
	}

	/** Where `instance`'s palette begins. Never assume 0: the arena reserves element 0 as its null. */
	uint32_t
	PaletteBaseOf(bgl::SceneView* view, bgl::MeshInstanceHandle instance)
	{
		auto  buffers      = view->GetInstanceBuffers();
		auto& meshBuffer   = std::get<1>(buffers);
		auto& skinnedState = std::get<3>(buffers);

		const bgl::idl::Mesh& mesh = meshBuffer.AtIndex(instance.handle.index);
		REQUIRE_FALSE(mesh.skinnedState.Null());

		return skinnedState.AtIndex(mesh.skinnedState.offset).palette.offsetStart;
	}

	void
	CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(1e-4));
		CHECK(actual.y == Catch::Approx(expected.y).margin(1e-4));
		CHECK(actual.z == Catch::Approx(expected.z).margin(1e-4));
	}
}

TEST_CASE("the pose pass writes the palette a rig's hierarchy implies", "[skinned][pose][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	const auto geom =
		scene
			->AddSkinnedMeshGeom(MakeSkinnedTriangle(), 0, materials, MakeChain(), MakeSwingClip());
	REQUIRE(geom.IsValid());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 1.0f, 8.0f),
			glm::vec3(0.0f, 1.0f, 7.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	auto* viewRaw = view->As<bgl::SceneView>();
	REQUIRE(viewRaw != nullptr);

	const uint32_t float4sPerPose = bgl::idl::cFloat4sPerBone * c_BoneCount;

	SECTION("a bind-pose frame gives an identity palette")
	{
		// rate 0 holds frame 0, which is the bind pose: pose * inverseBind is then exactly identity
		// for every bone. A wrong inverse bind, a missed hierarchy level and a mis-strided sample
		// fetch all break this, which is why it is the first thing asserted.
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const Palette palette =
			ReadPalette(gfxBase, viewRaw, PaletteBaseOf(viewRaw, instance), float4sPerPose);

		for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
		{
			CheckNear(
				palette.Apply(bone, glm::vec3(0.0f, float(bone), 0.0f)),
				glm::vec3(0.0f, float(bone), 0.0f));
			CheckNear(
				palette.Apply(bone, glm::vec3(1.0f, 2.0f, 3.0f)),
				glm::vec3(1.0f, 2.0f, 3.0f));
		}
	}

	SECTION("a parent's rotation reaches its grandchild")
	{
		// Frame 1 swings bone 1 by 90 degrees about +Z, about its own bind position (0,1,0).
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const Palette palette =
			ReadPalette(gfxBase, viewRaw, PaletteBaseOf(viewRaw, instance), float4sPerPose);

		// Bone 0 never moves: it is the root and its own local pose is unchanged.
		CheckNear(palette.Apply(0, glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(0.0f));

		// Bone 1 rotates about its own origin, so its bind position is a fixed point -- and a point
		// one unit above it swings onto -X.
		CheckNear(palette.Apply(1, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
		CheckNear(palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-1.0f, 1.0f, 0.0f));

		// Bone 2 has no local rotation of its own: everything it does here it inherited through the
		// walk. Its own base sits one unit above bone 1's pivot and its tip two, so the swing takes
		// them onto -X at those distances -- which is also the check that the walk composed exactly
		// once. Composing twice would double the offset; skipping the level would leave both at +Y.
		CheckNear(palette.Apply(2, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-1.0f, 1.0f, 0.0f));
		CheckNear(palette.Apply(2, glm::vec3(0.0f, 3.0f, 0.0f)), glm::vec3(-2.0f, 1.0f, 0.0f));
	}

	SECTION("a fractional frame blends the two it falls between")
	{
		// Half of a 90-degree swing is 45, and nlerp of the two endpoint quaternions is exactly the
		// half-angle rotation here (a single axis, so the shortest arc is unambiguous).
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.5f, 0.0f });
		gfx->DrawFrame(target, job);

		const Palette palette =
			ReadPalette(gfxBase, viewRaw, PaletteBaseOf(viewRaw, instance), float4sPerPose);

		const float c = std::cos(glm::radians(45.0f));
		const float s = std::sin(glm::radians(45.0f));

		// (0,1,0) relative to bone 1's origin, rotated 45 degrees about +Z.
		CheckNear(palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-s, 1.0f + c, 0.0f));
	}

	SECTION("growing the arena leaves every live instance's palette intact")
	{
		// A 3-bone slice is 18 float4s and the arena starts at 256, so the fifteenth instance forces a
		// Resize -- which *discards* the arena's contents. That is only safe because the pass reposes
		// every live instance every frame rather than the ones that changed, and this is what pins it:
		// after the growth, the first instance placed must still read back correctly.
		constexpr uint32_t c_Instances = 20;

		auto handles = std::vector<bgl::MeshInstanceHandle>();
		for (uint32_t i = 0; i < c_Instances; ++i)
		{
			handles.push_back(
				view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f }));
			REQUIRE(handles.back().IsValid());
		}

		REQUIRE(viewRaw->GetPalettes().Capacity() > 256);

		gfx->DrawFrame(target, job);
		CHECK(viewRaw->GetPosedInstanceCount() == c_Instances);

		// The first and the last, so both a slice allocated before the growth and one allocated after
		// it are covered.
		for (const uint32_t which : { 0u, c_Instances - 1 })
		{
			const Palette palette = ReadPalette(
				gfxBase,
				viewRaw,
				PaletteBaseOf(viewRaw, handles[which]),
				float4sPerPose);
			CheckNear(palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-1.0f, 1.0f, 0.0f));
			CheckNear(palette.Apply(2, glm::vec3(0.0f, 3.0f, 0.0f)), glm::vec3(-2.0f, 1.0f, 0.0f));
		}
	}

	SECTION("the prevTime palette sits a whole pose after the current one")
	{
		// rate 1 from phase 0: at time = one frame the current pose is frame 1 and the previous, one
		// frame of clock earlier, is frame 0 -- the bind pose. That pair is what a motion vector is
		// derived from, so the two halves must not be the same bytes.
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 1.0f });

		// Two frames, because prevTime equals time on the first one by construction (see ViewData) --
		// a single draw would compare a pose against itself and pass on a palette that never wrote
		// its second half.
		job.time = 0.0f;
		gfx->DrawFrame(target, job);

		job.time = 1.0f / c_SampleRate;
		gfx->DrawFrame(target, job);

		const Palette both =
			ReadPalette(gfxBase, viewRaw, PaletteBaseOf(viewRaw, instance), float4sPerPose * 2);

		// Current half: bone 1 swung.
		CheckNear(both.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-1.0f, 1.0f, 0.0f));

		// Previous half, one pose further in: still the bind pose, so identity.
		auto previous = Palette();
		previous.rows.assign(both.rows.begin() + float4sPerPose, both.rows.end());
		CheckNear(previous.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(0.0f, 2.0f, 0.0f));
	}
}
