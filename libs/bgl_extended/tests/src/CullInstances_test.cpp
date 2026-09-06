#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl_common/Frustum.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/CullStats.h>
#include <bgl_common/idl/CullView.h>
#include <bgl_common/idl/InstanceVisibility.h>
#include <bgl_common/idl/MeshInstance.h>
#include <bgl_common/idl/PsoType.h>
#include <bgl_common/idl/Submesh.h>
#include <bgl_common/idl/idl.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <cstdint>
#include <iterator>
#include <span>

// Drives the CullInstances kernel against a crafted scene: unit-radius spheres placed at known
// points around a known camera, one instance each. Reading back the visibility word proves the
// cull removes exactly what leaves the frustum and keeps exactly what stays -- the "it actually
// culls" claim the golden tests (all-visible scenes) cannot make.

namespace
{
	struct Placement
	{
		glm::vec3 position;
		bool      visible;
	};
}

TEST_CASE("Instances outside the frustum are culled, those inside survive", "[culling][compute]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// Eye at the origin looking down -Z, 90-degree fov, square aspect: at depth d the visible
	// half-extent is d. Every sphere is unit radius, so the margins below clear it comfortably.
	const glm::mat4 viewProj =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f)
			.GetViewProjection();

	const bgl::idl::CullView cullViewData = bgl::BuildCullView(viewProj);

	const Placement placements[] = {
		{ glm::vec3(0.0f, 0.0f, -10.0f), true },     // straight ahead
		{ glm::vec3(0.0f, 0.0f, -50.0f), true },     // deeper
		{ glm::vec3(5.0f, 0.0f, -20.0f), true },     // off-axis but inside
		{ glm::vec3(0.0f, 0.0f, 10.0f), false },     // behind the eye
		{ glm::vec3(0.0f, 0.0f, -200.0f), false },   // beyond the far plane
		{ glm::vec3(200.0f, 0.0f, -10.0f), false },  // past the right plane
	};
	constexpr uint32_t c_LiveCount = static_cast<uint32_t>(std::size(placements));
	const uint32_t     padded      = core::round_up(c_LiveCount, bgl::idl::cHistogramGroupSize);

	uint32_t expectedCulled = 0;
	for (const Placement& p : placements)
	{
		expectedCulled += p.visible ? 0u : 1u;
	}

	// One submesh, a unit sphere at its own origin, shared by every mesh. Each instance's world
	// bound is that sphere pushed out by its mesh transform.
	auto submeshBuffer = bgl::RangeBuffer<bgl::idl::Submesh>();
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 1;
		desc.debugName    = "Cull Submesh";
		submeshBuffer.Init(desc, resourceManager);
	}

	auto submesh            = bgl::idl::Submesh();
	submesh.boundingSphere  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	const auto submeshRange = submeshBuffer.Add(std::span<const bgl::idl::Submesh>(&submesh, 1));

	auto meshBuffer = bgl::EntryBuffer<bgl::idl::MeshInstance>();
	{
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = c_LiveCount;
		desc.debugName    = "Cull Mesh";
		meshBuffer.Init(desc, resourceManager);
	}

	auto instanceBuffer = bgl::PackedBuffer<bgl::SubmeshInstance>();
	{
		auto desc         = bgl::PackedBufferDesc();
		desc.initialCount = padded;
		desc.debugName    = "Cull Instances";
		instanceBuffer.Init(desc, resourceManager);
	}

	for (const Placement& p : placements)
	{
		auto mesh      = bgl::idl::MeshInstance();
		mesh.submeshes = submeshRange;
		bgl::WriteInstanceTransform(mesh, glm::translate(glm::mat4(1.0f), p.position));

		const auto meshHandle = meshBuffer.Add(mesh);

		auto instance         = bgl::SubmeshInstance();
		instance.meshInstance = meshHandle;
		instance.submeshIndex = 0u;
		instance.pso          = static_cast<uint32_t>(bgl::idl::PsoType::kOpaque_StaticMesh_PBR);
		instanceBuffer.Add(instance);
	}
	for (uint32_t i = c_LiveCount; i < padded; ++i)
	{
		// A default SubmeshInstance names no mesh; the shader skips it.
		instanceBuffer.Add(bgl::SubmeshInstance());
	}

	const auto makeCompute = [&](auto element, uint32_t count, const char* name) {
		auto buffer = bgl::ComputeBuffer();
		auto desc   = bgl::ComputeBufferDesc();
		desc.SetElement<decltype(element)>();
		desc.initialCount = count;
		desc.debugName    = name;
		buffer.Init(desc, resourceManager);
		return buffer;
	};

	auto cullView   = makeCompute(bgl::idl::CullView{}, 1, "Cull View");
	auto visibility = makeCompute(bgl::idl::InstanceVisibility{}, padded, "Visibility");
	auto stats      = makeCompute(bgl::idl::CullStats{}, 1, "Cull Stats");

	auto cull = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("programs.culling.CullInstances"))
			.SetDebugName("Cull Instances"));
	REQUIRE(cull.pipeline != nullptr);

	bgl::FrameGraph fg;
	fg.RegisterQueue("main", cmdQueue, cmdList);

	fg.ImportBuffer("instanceBuffer", instanceBuffer.GetBufferHandle());
	fg.ImportBuffer("meshBuffer", meshBuffer.GetBufferHandle());
	fg.ImportBuffer("submeshBuffer", submeshBuffer.GetBufferHandle());
	fg.ImportBuffer("cullView", cullView.GetBufferHandle());
	fg.ImportBuffer("visibility", visibility.GetBufferHandle());
	fg.ImportBuffer("stats", stats.GetBufferHandle());

	fg.AddPass(
		bgl::PassDesc()
			.SetName("Upload")
			.AddBufferArg(
				"instanceBuffer",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"meshBuffer",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"submeshBuffer",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"cullView",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"visibility",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg("stats", bgl::BarrierSyncFlag::kCopy, bgl::BarrierAccessFlag::kCopyDest)
			.SetExec([&](const bgl::PassContext& ctx) {
				auto* cmd = ctx.GetCommandList();
				submeshBuffer.Update(cmd);
				meshBuffer.Update(cmd);
				instanceBuffer.Update(cmd);
				visibility.Clear(cmd);
				stats.Clear(cmd);
				cmd->WriteBuffer(
					cullView.GetBufferHandle(),
					&cullViewData,
					sizeof(bgl::idl::CullView));
			}));

	fg.AddPass(
		bgl::PassDesc()
			.SetName("Cull")
			.AddBufferArg(
				"instanceBuffer",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				"meshBuffer",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				"submeshBuffer",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				"cullView",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"visibility",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"stats",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.SetExec([&](const bgl::PassContext& ctx) {
				auto* cmd = ctx.GetCommandList();

				cull["gUniforms"]["cullView"]       = cullView.GetBufferHandle();
				cull["gUniforms"]["instanceBuffer"] = instanceBuffer.GetBufferHandle();
				cull["gUniforms"]["meshBuffer"]     = meshBuffer.GetBufferHandle();
				cull["gUniforms"]["submeshBuffer"]  = submeshBuffer.GetBufferHandle();
				cull["gUniforms"]["visibility"]     = visibility.GetBufferHandle();
#if defined(BERNINI_GPU_DEBUG)
				cull["gUniforms"]["stats"] = stats.GetBufferHandle();
#endif

				auto state   = bgl::ComputeState();
				state.kernel = &cull;
				cmd->SetComputeState(state);
				cmd->Dispatch(core::div_ceil(padded, bgl::idl::cHistogramGroupSize), 1, 1);
			}));

	fg.Compile(resourceManager.Get());

	auto rbDesc       = bgl::ReadbackBufferDesc();
	rbDesc.byteSize   = static_cast<uint64_t>(padded) * sizeof(bgl::idl::InstanceVisibility);
	rbDesc.debugName  = "Visibility Readback";
	auto rbVisibility = resourceManager->CreateReadbackBuffer(rbDesc);

	rbDesc.byteSize  = sizeof(bgl::idl::CullStats);
	rbDesc.debugName = "Cull Stats Readback";
	auto rbStats     = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	fg.Execute();

	const auto toCopySource = []() {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
		    .AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
		    .AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		    .AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
	};

	cmdList->Barrier(visibility.GetBufferHandle(), toCopySource());
	cmdList->CopyBufferToReadback(rbVisibility, visibility.GetBufferHandle());

	cmdList->Barrier(stats.GetBufferHandle(), toCopySource());
	cmdList->CopyBufferToReadback(rbStats, stats.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* visibilityOut = static_cast<const bgl::idl::InstanceVisibility*>(
		resourceManager->MapReadback(rbVisibility));
	REQUIRE(visibilityOut != nullptr);

	uint32_t visibleCount = 0;
	for (uint32_t i = 0; i < c_LiveCount; ++i)
	{
		CAPTURE(i);
		CHECK((visibilityOut[i].visible != 0u) == placements[i].visible);
		visibleCount += visibilityOut[i].visible != 0u ? 1u : 0u;
	}
	CHECK(visibleCount == c_LiveCount - expectedCulled);

	// Padding slots name no mesh, so the cull writes them 0.
	for (uint32_t i = c_LiveCount; i < padded; ++i)
	{
		CHECK(visibilityOut[i].visible == 0u);
	}
	resourceManager->UnmapReadback(rbVisibility);

#if defined(BERNINI_GPU_DEBUG)
	// Stats are written only in debug builds; the readback is meaningful only there.
	const auto* statsOut =
		static_cast<const bgl::idl::CullStats*>(resourceManager->MapReadback(rbStats));
	REQUIRE(statsOut != nullptr);
	CHECK(statsOut->tested == c_LiveCount);            // every live instance is tested
	CHECK(statsOut->frustumCulled == expectedCulled);  // and the outside ones are counted culled
	resourceManager->UnmapReadback(rbStats);
#endif

	resourceManager->DestroyReadbackBuffer(rbVisibility, false);
	resourceManager->DestroyReadbackBuffer(rbStats, false);

	instanceBuffer.Release(false);
	meshBuffer.Release(false);
	submeshBuffer.Release(false);
	cullView.Release(false);
	visibility.Release(false);
	stats.Release(false);
}
