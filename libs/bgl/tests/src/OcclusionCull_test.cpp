#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/Frustum.h"
#include "gfx/GraphicsBase.h"
#include "scene/HzbChain.h"
#include "types/SubmeshInstance.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/util.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/CullStats.h>
#include <bgl/idl/CullView.h>
#include <bgl/idl/DrawBucket.h>
#include <bgl/idl/Geom.h>
#include <bgl/idl/InstanceLod.h>
#include <bgl/idl/InstanceVisibility.h>
#include <bgl/idl/MeshInstance.h>
#include <bgl/idl/Submesh.h>
#include <bgl/types/Camera.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/buffer/PackedBuffer.h>
#include <bgpu/buffer/RangeBuffer.h>
#include <bgpu/buffer/UploadBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Phase 1 of the occlusion cull, driven against a crafted ladder: every level one depth, the
// occluder every texel holds. Unit spheres in front of it draw, behind it they are candidates for
// phase 2, one crossing the near plane draws whatever stands in front, one not drawn last frame is
// a candidate without a test, a bucket that is no occludee is never tested, and a ladder the view
// says is not valid tests nothing. The visibility word is the proof; the drawn-history word that
// phase 2 reads is checked beside it.

namespace
{
	// The depth buffer the ladder stands for: level 0 is half of it, down to one texel.
	constexpr uint32_t c_DepthSize = 128;

	enum class Verdict : uint8_t
	{
		kDraws,
		kCandidate,
	};

	struct Placement
	{
		glm::vec3 position;
		Verdict   verdict;
		bool      drawnLastFrame = true;
		uint32_t  bucket         = 2;
	};

	// The reversed-Z clip depth of a view-space z under the camera below: what the ladder's one
	// value is chosen against.
	float
	ClipDepth(const glm::mat4& viewProj, const glm::vec3& world)
	{
		const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
		return clip.z / clip.w;
	}
}

TEST_CASE(
	"Phase 1 draws the occludees last frame's HZB does not hide and leaves the rest to phase 2",
	"[culling][occlusion][compute]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	// Eye at the origin looking down -Z, 90-degree fov, square aspect.
	const glm::mat4 viewProj =
		bgl::Camera()
			.LookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f)
			.GetViewProjection();

	// The occluder: a wall at z = -30 everywhere. Nearer is greater under reversed-Z, so a sphere
	// whose nearest point is beyond the wall reads a smaller depth than the ladder holds.
	const float wall = ClipDepth(viewProj, glm::vec3(0.0f, 0.0f, -30.0f));
	REQUIRE(ClipDepth(viewProj, glm::vec3(0.0f, 0.0f, -10.0f)) > wall);
	REQUIRE(ClipDepth(viewProj, glm::vec3(0.0f, 0.0f, -50.0f)) < wall);

	const Placement placements[] = {
		{ glm::vec3(0.0f, 0.0f, -10.0f), Verdict::kDraws },             // in front of the wall
		{ glm::vec3(5.0f, 2.0f, -20.0f), Verdict::kDraws },             // in front, off-axis
		{ glm::vec3(0.0f, 0.0f, -50.0f), Verdict::kCandidate },         // behind the wall
		{ glm::vec3(-8.0f, 3.0f, -70.0f), Verdict::kCandidate },        // behind, off-axis
		{ glm::vec3(0.0f, 0.0f, -1.2f), Verdict::kDraws },              // crossing the near plane
		{ glm::vec3(0.0f, 0.0f, -10.0f), Verdict::kCandidate, false },  // not drawn last frame
		{ glm::vec3(0.0f, 0.0f, -50.0f), Verdict::kDraws, true, 3 },    // behind, but no occludee
	};
	constexpr uint32_t c_LiveCount = static_cast<uint32_t>(std::size(placements));
	const uint32_t     padded      = core::round_up(c_LiveCount, bgl::idl::cHistogramGroupSize);

	auto submeshBuffer = bgpu::RangeBuffer<bgl::idl::Submesh>(
		resourceManager,
		bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Cull Submesh"));

	auto submesh            = bgl::idl::Submesh();
	submesh.boundingSphere  = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	const auto submeshRange = submeshBuffer.Add(std::span<const bgl::idl::Submesh>(&submesh, 1));

	auto geomBuffer = bgpu::EntryBuffer<bgl::idl::Geom>(
		resourceManager,
		bgpu::EntryBufferDesc().SetInitialCount(1).SetDebugName("Cull Geom"));

	auto geomRecord                   = bgl::idl::Geom();
	geomRecord.submeshes.range        = submeshRange;
	geomRecord.submeshes.submeshCount = 1u;
	geomRecord.submeshes.lodCount     = 1u;
	const auto geomHandle             = geomBuffer.Add(geomRecord);

	auto meshBuffer = bgpu::EntryBuffer<bgl::idl::MeshInstance>(
		resourceManager,
		bgpu::EntryBufferDesc().SetInitialCount(c_LiveCount).SetDebugName("Cull Mesh"));

	auto instanceBuffer = bgpu::PackedBuffer<bgl::SubmeshInstance>(
		resourceManager,
		bgpu::PackedBufferDesc().SetInitialCount(padded).SetDebugName("Cull Instances"));

	auto history = std::vector<uint32_t>(padded, 0u);
	for (uint32_t i = 0; i < c_LiveCount; ++i)
	{
		const Placement& p = placements[i];

		auto mesh = bgl::idl::MeshInstance();
		mesh.geom = geomHandle;
		bgl::WriteInstanceTransform(mesh, glm::translate(glm::mat4(1.0f), p.position));
		const auto meshHandle = meshBuffer.Add(mesh);

		auto instance         = bgl::SubmeshInstance();
		instance.meshInstance = meshHandle;
		instance.submeshIndex = 0u;
		instance.drawBucket   = p.bucket;
		instanceBuffer.Add(instance);

		history[i] = p.drawnLastFrame ? 1u : 0u;
	}
	for (uint32_t i = c_LiveCount; i < padded; ++i)
	{
		instanceBuffer.Add(bgl::SubmeshInstance());
	}

	const auto makeCompute = [&](auto element, uint32_t count, const char* name) {
		return bgpu::ComputeBuffer(
			resourceManager,
			bgpu::ComputeBufferDesc()
				.SetElement<decltype(element)>()
				.SetInitialCount(count)
				.SetDebugName(name));
	};

	// The ladder: a 128x128 depth's levels, 64 down to 1, every texel the wall's depth.
	auto chain = bgl::HzbChain();
	REQUIRE(chain.Ensure(resourceManager, c_DepthSize, c_DepthSize));
	const std::span<const bgl::HzbChain::Level> levels = chain.GetLevels();
	REQUIRE(levels.size() == 7);

	auto cullViewData          = bgl::BuildCullView(viewProj);
	cullViewData.prevViewProj  = viewProj;
	cullViewData.hzbRect       = glm::vec4(0.0f, 0.0f, 64.0f, 64.0f);
	cullViewData.hzbLevel0Size = glm::vec2(64.0f, 64.0f);
	cullViewData.hzbLevelCount = static_cast<uint32_t>(levels.size());
	cullViewData.occlusion     = bgl::idl::cOcclusionTestBit | bgl::idl::cOcclusionHzbValidBit;

	auto cullView = bgpu::UploadBuffer<bgl::idl::CullView>(
		resourceManager,
		bgpu::UploadBufferDesc().SetInitialCount(1).SetDebugName("Cull View"));
	cullView.Assign(std::span(&cullViewData, 1));

	auto flagWords = std::vector<uint32_t>(bgl::idl::cMaxDrawBuckets, 0u);
	flagWords[2]   = std::to_underlying(bgl::idl::DrawBucketFlag::kOccludee);
	auto flags     = bgpu::UploadBuffer<uint32_t>(
		resourceManager,
		bgpu::UploadBufferDesc().SetInitialCount(bgl::idl::cMaxDrawBuckets).SetDebugName("Flags"));
	flags.Assign(flagWords);

	auto visibility  = makeCompute(bgl::idl::InstanceVisibility{}, padded, "Visibility");
	auto drawn       = makeCompute(uint32_t{}, padded, "Drawn History");
	auto stats       = makeCompute(bgl::idl::CullStats{}, 1, "Cull Stats");
	auto lodPrevious = makeCompute(bgl::idl::InstanceLod{}, c_LiveCount + 1, "Lod Previous");
	auto lodCurrent  = makeCompute(bgl::idl::InstanceLod{}, c_LiveCount + 1, "Lod Current");

	const auto sampler = resourceManager->CreateSampler(bgpu::SamplerDesc());

	auto cull = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("programs.culling.CullInstances"))
			.SetDebugName("Cull Instances"));
	REQUIRE(cull.pipeline != nullptr);

	// The wall, drawn into every level: a clear is what a render target is cleared with.
	{
		cmdList->Open(cmdQueue, cmdAllocator);
		float wallColor[4] = { wall, 0.0f, 0.0f, 0.0f };
		for (const bgl::HzbChain::Level& level : levels)
		{
			resourceManager->ClearRtv(cmdList.Get(), level.rtv, wallColor);
		}
		cmdList->WriteBuffer(
			drawn.GetBufferHandle(),
			history.data(),
			0,
			history.size() * sizeof(uint32_t));
		cmdList->Close();
		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));
		cmdAllocator->ResetAllocator();
	}

	const auto runCull = [&](const bgl::idl::CullView& viewData) {
		cullView.Assign(std::span(&viewData, 1));

		bgl::FrameGraph fg;
		fg.RegisterQueue("main", cmdQueue, cmdList);

		fg.ImportBuffer("instanceBuffer", instanceBuffer.GetBufferHandle());
		fg.ImportBuffer("meshBuffer", meshBuffer.GetBufferHandle());
		fg.ImportBuffer("geomBuffer", geomBuffer.GetBufferHandle());
		fg.ImportBuffer("submeshBuffer", submeshBuffer.GetBufferHandle());
		fg.ImportBuffer("cullView", cullView.GetBufferHandle());
		fg.ImportBuffer("flags", flags.GetBufferHandle());
		fg.ImportBuffer("visibility", visibility.GetBufferHandle());
		fg.ImportBuffer("drawn", drawn.GetBufferHandle());
		fg.ImportBuffer("stats", stats.GetBufferHandle());
		fg.ImportBuffer("lodPrevious", lodPrevious.GetBufferHandle());
		fg.ImportBuffer("lodCurrent", lodCurrent.GetBufferHandle());
		for (uint32_t i = 0; i < levels.size(); ++i)
		{
			fg.ImportTexture(
				bgl::HzbLevelName(i),
				levels[i].texture,
				bgl::AccessState{ bgpu::BarrierSyncFlag::kRenderTarget,
			                      bgpu::BarrierAccessFlag::kRenderTarget,
			                      bgpu::BarrierLayout::kRenderTarget });
		}

		fg.AddPass(
			bgl::PassDesc()
				.SetName("Upload")
				.AddCopyDest("instanceBuffer")
				.AddCopyDest("meshBuffer")
				.AddCopyDest("geomBuffer")
				.AddCopyDest("submeshBuffer")
				.AddCopyDest("cullView")
				.AddCopyDest("flags")
				.AddCopyDest("visibility")
				.AddCopyDest("stats")
				.AddCopyDest("lodPrevious")
				.AddCopyDest("lodCurrent")
				.SetExec([&](const bgl::PassContext& ctx) {
					auto* cmd = ctx.GetCommandList();
					submeshBuffer.Update(cmd);
					geomBuffer.Update(cmd);
					meshBuffer.Update(cmd);
					instanceBuffer.Update(cmd);
					visibility.Clear(cmd);
					stats.Clear(cmd);
					lodPrevious.Clear(cmd);
					lodCurrent.Clear(cmd);
					cullView.Update(cmd);
					flags.Update(cmd);
				}));

		auto desc = bgl::PassDesc();
		desc.SetName("Cull")
			.AddBufferRead("instanceBuffer", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("meshBuffer", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("geomBuffer", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("submeshBuffer", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("cullView", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("flags", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferReadWrite("visibility", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferReadWrite("drawn", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferReadWrite("stats", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferRead("lodPrevious", bgpu::BarrierSyncFlag::kComputeShader)
			.AddBufferReadWrite("lodCurrent", bgpu::BarrierSyncFlag::kComputeShader);
		for (uint32_t i = 0; i < levels.size(); ++i)
		{
			desc.AddTextureRead(bgl::HzbLevelName(i), bgpu::BarrierSyncFlag::kComputeShader);
		}
		desc.SetExec([&](const bgl::PassContext& ctx) {
			auto* cmd = ctx.GetCommandList();

			cull["gUniforms"]["cullView"]        = cullView.GetBufferHandle();
			cull["gUniforms"]["instanceBuffer"]  = instanceBuffer.GetBufferHandle();
			cull["gUniforms"]["meshBuffer"]      = meshBuffer.GetBufferHandle();
			cull["gUniforms"]["geomBuffer"]      = geomBuffer.GetBufferHandle();
			cull["gUniforms"]["submeshBuffer"]   = submeshBuffer.GetBufferHandle();
			cull["gUniforms"]["visibility"]      = visibility.GetBufferHandle();
			cull["gUniforms"]["lodPrevious"]     = lodPrevious.GetBufferHandle();
			cull["gUniforms"]["lodCurrent"]      = lodCurrent.GetBufferHandle();
			cull["gUniforms"]["drawBucketFlags"] = flags.GetBufferHandle();
			cull["gUniforms"]["drawnHistory"]    = drawn.GetBufferHandle();
			cull["gUniforms"]["hzbSampler"]      = sampler;
			for (uint32_t i = 0; i < levels.size(); ++i)
			{
				cull["gUniforms"]["hzb" + std::to_string(i)] = levels[i].srv;
			}
#if defined(BERNINI_GPU_DEBUG)
			cull["gUniforms"]["stats"] = stats.GetBufferHandle();
#endif

			auto state   = bgpu::ComputeState();
			state.kernel = &cull;
			cmd->SetComputeState(state);
			cmd->Dispatch(core::div_ceil(padded, bgl::idl::cHistogramGroupSize), 1, 1);
		});
		fg.AddPass(std::move(desc));

		fg.Compile(resourceManager.Get());

		auto rbDesc       = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize   = static_cast<uint64_t>(padded) * sizeof(bgl::idl::InstanceVisibility);
		rbDesc.debugName  = "Visibility Readback";
		auto rbVisibility = resourceManager->CreateReadbackBuffer(rbDesc);

		rbDesc.byteSize  = static_cast<uint64_t>(padded) * sizeof(uint32_t);
		rbDesc.debugName = "Drawn Readback";
		auto rbDrawn     = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);
		fg.Execute();

		const auto toCopySource = []() {
			return bgpu::BufferBarrierDesc()
			    .AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			    .AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			    .AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			    .AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource);
		};
		cmdList->Barrier(visibility.GetBufferHandle(), toCopySource());
		cmdList->CopyBufferToReadback(rbVisibility, visibility.GetBufferHandle());
		cmdList->Barrier(drawn.GetBufferHandle(), toCopySource());
		cmdList->CopyBufferToReadback(rbDrawn, drawn.GetBufferHandle());
		cmdList->Close();

		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));
		cmdAllocator->ResetAllocator();

		const auto* words = static_cast<const bgl::idl::InstanceVisibility*>(
			resourceManager->MapReadback(rbVisibility));
		REQUIRE(words != nullptr);
		auto visible = std::vector<uint32_t>(c_LiveCount);
		for (uint32_t i = 0; i < c_LiveCount; ++i)
		{
			visible[i] = words[i].visible;
		}
		resourceManager->UnmapReadback(rbVisibility);

		const auto* drawnWords =
			static_cast<const uint32_t*>(resourceManager->MapReadback(rbDrawn));
		REQUIRE(drawnWords != nullptr);
		auto drawnNow = std::vector<uint32_t>(drawnWords, drawnWords + c_LiveCount);
		resourceManager->UnmapReadback(rbDrawn);

		resourceManager->DestroyReadbackBuffer(rbVisibility, false);
		resourceManager->DestroyReadbackBuffer(rbDrawn, false);
		return std::pair(visible, drawnNow);
	};

	SECTION("against a valid ladder")
	{
		const auto [visible, drawnNow] = runCull(cullViewData);
		for (uint32_t i = 0; i < c_LiveCount; ++i)
		{
			CAPTURE(i);
			const bool draws = placements[i].verdict == Verdict::kDraws;
			CHECK(
				visible[i] ==
				(draws ? bgl::idl::cVisibleCurrentBit : bgl::idl::cVisibleCandidateBit));
			CHECK(drawnNow[i] == (draws ? 1u : 0u));
		}
	}

	SECTION("with no valid ladder every occludee draws")
	{
		auto noLadder                  = cullViewData;
		noLadder.occlusion             = bgl::idl::cOcclusionTestBit;
		const auto [visible, drawnNow] = runCull(noLadder);
		for (uint32_t i = 0; i < c_LiveCount; ++i)
		{
			CAPTURE(i);
			CHECK(visible[i] == bgl::idl::cVisibleCurrentBit);
			CHECK(drawnNow[i] == 1u);
		}
	}

	SECTION("with the test off nothing is a candidate")
	{
		auto off                       = cullViewData;
		off.occlusion                  = bgl::idl::cOcclusionHzbValidBit;
		const auto [visible, drawnNow] = runCull(off);
		for (uint32_t i = 0; i < c_LiveCount; ++i)
		{
			CAPTURE(i);
			CHECK(visible[i] == bgl::idl::cVisibleCurrentBit);
		}
	}

	resourceManager->DestroySampler(sampler);
}
