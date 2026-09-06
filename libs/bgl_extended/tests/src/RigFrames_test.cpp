#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "types/Barrier.h"
#include "types/QueueType.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IGraphics.h>
#include <bgl/RigHandle.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Constants.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ratio>
#include <span>
#include <vector>

// The bone anim table: every frame of every clip of a rig, posed once by RigFramesPass and read by
// every instance that draws from it instead of computing its own pose.
//
// What these pin is that the table *is* the pose, against assetlib's CPU reference -- which is what
// every GPU path here is diffed against, the per-instance pose pass included, so the two producers
// are held to one answer without being compared to each other.

namespace
{
	constexpr uint32_t c_BoneCount  = 3;
	constexpr float    c_SampleRate = 30.0f;

	/** A three-bone chain up +Y, each inverse bind the exact inverse of its bind pose. */
	assetlib::Skeleton
	MakeChain()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_BoneCount; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/**
	 * Two clips back to back, so the fill has to find which clip a global frame belongs to rather
	 * than assuming one: a two-frame swing, then a three-frame one that bends a different bone.
	 */
	assetlib::AnimationSet
	MakeTwoClips(const assetlib::Skeleton& skeleton)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		// assetlib's own pose reference refuses a pair whose signatures disagree, and it is the
		// reference these cases are checked against.
		set.skeletonSignature = assetlib::skeletonSignature(skeleton);

		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);
		const auto half  = glm::quat(0.92387953f, 0.0f, 0.0f, 0.38268343f);

		const auto push = [&](uint32_t frames, uint32_t bendBone, const glm::quat& rot) {
			for (uint32_t f = 0; f < frames; ++f)
			{
				for (uint32_t b = 0; b < c_BoneCount; ++b)
				{
					auto sample        = assetlib::Transform();
					sample.translation = glm::vec3(0.0f, b == 0 ? 0.0f : 1.0f, 0.0f);
					sample.rotation =
						(f > 0 && b == bendBone) ? rot : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
					sample.scale = glm::vec3(1.0f);
					set.samples.push_back(sample);
				}
			}
		};

		push(2, 1, swing);
		push(3, 2, half);

		const auto clipAt = [&](uint32_t firstSample, uint32_t frames) {
			auto clip        = assetlib::AnimationClip();
			clip.firstSample = firstSample;
			clip.frameCount  = frames;
			clip.sampleRate  = c_SampleRate;
			clip.duration    = float(frames - 1) / c_SampleRate;
			clip.loop        = 0;
			clip.nameOffset  = 0;
			return clip;
		};

		set.clips.push_back(clipAt(0, 2));
		set.clips.push_back(clipAt(2 * c_BoneCount, 3));

		return set;
	}

	/** The whole bone anim table arena, as float4 rows. */
	std::vector<glm::vec4>
	ReadTables(bgl::GraphicsBase* gfxBase, bgl::Scene* scene)
	{
		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();

		// This copy rides its own queue, which nothing orders against the frame that filled the
		// table.
		gfxBase->WaitIdle();

		auto cmdListDesc = bgl::CommandListDesc();
		cmdListDesc.type = bgl::QueueType::kGraphics;

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		cmdAllocator->ResetAllocator();

		const bgl::BufferHandle tables = scene->GetBoneAnimTables().GetBufferHandle();

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = uint64_t(scene->GetBoneAnimTables().Capacity()) * sizeof(glm::vec4);
		rbDesc.debugName = "Bone Anim Table Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(tables, barrier);

		cmdList->CopyBufferToReadback(rb, tables);
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);

		auto rows = std::vector<glm::vec4>(mapped, mapped + scene->GetBoneAnimTables().Capacity());

		resourceManager->UnmapReadback(rb);
		return rows;
	}

	/** `point` skinned by the matrix the three rows at `row` hold. */
	glm::vec3
	ApplyRows(std::span<const glm::vec4> rows, size_t row, const glm::vec3& point)
	{
		const glm::vec4 p(point, 1.0f);
		return { glm::dot(rows[row + 0], p),
			     glm::dot(rows[row + 1], p),
			     glm::dot(rows[row + 2], p) };
	}

	void
	CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		CHECK(actual.x == Catch::Approx(expected.x).margin(1e-4));
		CHECK(actual.y == Catch::Approx(expected.y).margin(1e-4));
		CHECK(actual.z == Catch::Approx(expected.z).margin(1e-4));
	}

	bgl::SceneDesc
	TestSceneDesc()
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 4;
		desc.initialMeshlets             = 8;
		desc.initialSubmeshes            = 4;
		desc.initialVertexBufferByteSize = 4096;
		desc.initialIndices              = 64;
		desc.initialPbrMaterials         = 4;
		return desc;
	}
}

TEST_CASE("a rig's bone anim table holds every frame of every clip", "[skinned][rigframes][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = dynamic_cast<bgl::GraphicsBase*>(gfx.Get());
	REQUIRE(gfxBase != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 32;
	targetDesc.height   = 32;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto view = gfx->CreateSceneView(sceneHandle, 4);
	bgl::test::ApplyEnvironment(sceneHandle.Get(), view.Get());

	const assetlib::Skeleton     skeleton   = MakeChain();
	const assetlib::AnimationSet animations = MakeTwoClips(skeleton);

	const bgl::RigHandle rig = scene->AddRig(skeleton, animations);
	REQUIRE(rig.IsValid());

	// Nothing draws from it yet -- task 3 is what spawns such an instance -- so the request is made
	// here directly, which is also the door the editor's preview will take.
	scene->RequestBoneAnimTable(rig);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(32.0f, 32.0f);
	gfx->DrawFrame(target, job);

	const std::vector<glm::vec4> rows = ReadTables(gfxBase, scene);

	const uint32_t base = scene->GetRigBuffer()[rig.handle].boneAnimTable.offsetStart;
	REQUIRE(base != 0);

	// The points a skinning matrix is compared through, rather than the matrix itself: the CPU
	// reference is column-major glm and the table holds three rows as the shader indexed them, so
	// what both agree on is where a point lands.
	const std::array<glm::vec3, 3> probes = {
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.5f, -0.25f, 2.0f } }
	};

	for (uint32_t clip = 0; clip < animations.clips.size(); ++clip)
	{
		const uint32_t frames = animations.clips[clip].frameCount;

		for (uint32_t frame = 0; frame < frames; ++frame)
		{
			const std::vector<glm::mat4> expected = assetlib::skinningMatrices(
				skeleton,
				assetlib::poseModelTransforms(skeleton, animations, clip, frame));

			// Frame-major and global across clips, the same addressing the sample pool uses.
			const uint32_t globalFrame = animations.clips[clip].firstSample / c_BoneCount + frame;

			for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
			{
				const size_t row =
					base + (size_t(globalFrame) * c_BoneCount + bone) * bgl::idl::cFloat4sPerBone;

				for (const glm::vec3& probe : probes)
				{
					CheckNear(
						ApplyRows(rows, row, probe),
						glm::vec3(expected[bone] * glm::vec4(probe, 1.0f)));
				}
			}
		}
	}
}

// The pass writes an imported buffer, so the frame graph keeps it as a root however little it does
// -- an Execute early-return would leave a node and a UAV transition in every frame of every view.
// AttachToFrameGraph asks this predicate instead, so what it answers is what a scene drawing no
// crowd instance pays. There is no seam onto the graph's kept passes to assert against directly.
TEST_CASE(
	"a scene pays for the fill pass only on the frame that fills",
	"[skinned][rigframes][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 32;
	targetDesc.height   = 32;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto view = gfx->CreateSceneView(sceneHandle, 4);
	bgl::test::ApplyEnvironment(sceneHandle.Get(), view.Get());

	const assetlib::Skeleton     skeleton   = MakeChain();
	const assetlib::AnimationSet animations = MakeTwoClips(skeleton);

	const bgl::RigHandle rig = scene->AddRig(skeleton, animations);
	REQUIRE(rig.IsValid());

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(32.0f, 32.0f);

	// A rig alone asks for nothing: a table is wanted by an instance drawing from one.
	CHECK(scene->PendingRigFills().empty());
	gfx->DrawFrame(target, job);
	CHECK(scene->PendingRigFills().empty());

	scene->RequestBoneAnimTable(rig);
	CHECK(scene->PendingRigFills().size() == 1);

	// The frame that fills it is the one frame the pass is attached to.
	gfx->DrawFrame(target, job);

	CHECK(scene->PendingRigFills().empty());
	gfx->DrawFrame(target, job);
	CHECK(scene->PendingRigFills().empty());
}

TEST_CASE("a growth of the arena leaves every filled table intact", "[skinned][rigframes][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = dynamic_cast<bgl::GraphicsBase*>(gfx.Get());
	REQUIRE(gfxBase != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 32;
	targetDesc.height   = 32;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto view = gfx->CreateSceneView(sceneHandle, 4);
	bgl::test::ApplyEnvironment(sceneHandle.Get(), view.Get());

	const assetlib::Skeleton     skeleton   = MakeChain();
	const assetlib::AnimationSet animations = MakeTwoClips(skeleton);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(32.0f, 32.0f);

	const bgl::RigHandle first = scene->AddRig(skeleton, animations);
	scene->RequestBoneAnimTable(first);
	gfx->DrawFrame(target, job);

	const uint32_t firstBase = scene->GetRigBuffer()[first.handle].boneAnimTable.offsetStart;
	const uint32_t capacity  = scene->GetBoneAnimTables().Capacity();

	// Keep asking until the arena has to grow. The growth discards what it held, so the first rig's
	// table only survives if the growth re-queued it -- which is the whole point of this case.
	auto held = std::vector<bgl::RigHandle>();
	while (scene->GetBoneAnimTables().Capacity() == capacity)
	{
		const bgl::RigHandle next = scene->AddRig(skeleton, animations);
		scene->RequestBoneAnimTable(next);
		held.push_back(next);

		REQUIRE(held.size() < 4096);
	}

	gfx->DrawFrame(target, job);

	const std::vector<glm::vec4> rows = ReadTables(gfxBase, scene);

	// The first rig kept its offset -- allocations survive a growth -- and its pose is back.
	CHECK(scene->GetRigBuffer()[first.handle].boneAnimTable.offsetStart == firstBase);

	const std::vector<glm::mat4> expected = assetlib::skinningMatrices(
		skeleton,
		assetlib::poseModelTransforms(skeleton, animations, 0, 1));

	for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
	{
		const size_t row = firstBase + (size_t(1) * c_BoneCount + bone) * bgl::idl::cFloat4sPerBone;

		const glm::vec3 probe(0.25f, 1.0f, -0.5f);
		CheckNear(ApplyRows(rows, row, probe), glm::vec3(expected[bone] * glm::vec4(probe, 1.0f)));
	}
}

// Hidden: it allocates ~72 MB of device memory and poses 1.5M bones, which is a cost the suite
// should not pay on every run. Run it by hand when the fill's cost is the question --
// `just run bgl_extended_tests -- "[.rigtiming]"` -- and read the numbers off the log's stage lines.
TEST_CASE("what a dense rig's table costs to stand up", "[.rigtiming]")
{
	// cha800_00's shape, which is the largest rig the project holds: 663 bones, 2,254 frames.
	constexpr uint32_t c_Bones  = 663;
	constexpr uint32_t c_Frames = 2254;

	auto opts           = bgl::GraphicsOptions();
	opts.shaderCacheDir = bgl::test::ShaderCacheDir();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 32;
	targetDesc.height   = 32;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto  sceneHandle = gfx->CreateScene(TestSceneDesc());
	auto* scene       = sceneHandle->As<bgl::Scene>();
	REQUIRE(scene != nullptr);

	auto view = gfx->CreateSceneView(sceneHandle, 4);
	bgl::test::ApplyEnvironment(sceneHandle.Get(), view.Get());

	auto skeleton = assetlib::Skeleton();
	for (uint32_t i = 0; i < c_Bones; ++i)
	{
		auto bone        = assetlib::Bone();
		bone.bindPose    = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
			                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			                 glm::vec3(1.0f) };
		bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
		bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
		bone.nameOffset  = 0;
		skeleton.bones.push_back(bone);
	}

	auto animations              = assetlib::AnimationSet();
	animations.boneCount         = c_Bones;
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);
	animations.samples.resize(size_t(c_Bones) * c_Frames);
	for (size_t i = 0; i < animations.samples.size(); ++i)
	{
		animations.samples[i].translation = glm::vec3(0.0f, (i % c_Bones) == 0 ? 0.0f : 1.0f, 0.0f);
		animations.samples[i].rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		animations.samples[i].scale       = glm::vec3(1.0f);
	}

	auto clip        = assetlib::AnimationClip();
	clip.firstSample = 0;
	clip.frameCount  = c_Frames;
	clip.sampleRate  = c_SampleRate;
	clip.duration    = float(c_Frames - 1) / c_SampleRate;
	animations.clips.push_back(clip);

	const bgl::RigHandle rig = scene->AddRig(skeleton, animations);
	REQUIRE(rig.IsValid());

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(32.0f, 32.0f);

	// A frame with nothing to fill, as the baseline the filling frame is read against.
	gfx->DrawFrame(target, job);

	const auto reserveStart = std::chrono::steady_clock::now();
	scene->RequestBoneAnimTable(rig);
	const auto reserveMs =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - reserveStart)
			.count();

	const auto fillStart = std::chrono::steady_clock::now();
	gfx->DrawFrame(target, job);
	const auto fillMs =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fillStart)
			.count();

	const auto idleStart = std::chrono::steady_clock::now();
	gfx->DrawFrame(target, job);
	const auto idleMs =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idleStart)
			.count();

	// WARN rather than a log line: this case exists to report, and Catch2 prints it either way.
	WARN(
		std::format(
			"rig table timing: {} bones x {} frames = {} MiB | reserve {:.1f} ms | filling frame "
			"{:.1f} ms | next frame {:.1f} ms",
			c_Bones,
			c_Frames,
			(uint64_t(c_Bones) * c_Frames * bgl::idl::cFloat4sPerBone * sizeof(glm::vec4)) >> 20,
			reserveMs,
			fillMs,
			idleMs));

	CHECK(scene->GetRigBuffer()[rig.handle].boneAnimTable.offsetStart != 0);
}
