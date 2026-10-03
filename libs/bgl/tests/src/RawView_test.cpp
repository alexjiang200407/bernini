#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/idl/AutoPosedInstance.h>
#include <bgl/idl/BlobShadow.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/CullView.h>
#include <bgl/idl/DominantFrames.h>
#include <bgl/idl/InstancePose.h>
#include <bgl/idl/Rig.h>
#include <bgl/idl/SkinnedAutoState.h>
#include <bgl/idl/SkinnedTableState.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
	// Where each thing under test sits. The records are past 0 so a wrong base cannot pass; the
	// loose values are at offsets that are 4-aligned and deliberately not 16-aligned, which is all
	// a vertex attribute is ever promised (an importer packs attributes sequentially by format
	// size, so a tangent lands at 20 in a layout carrying no normal).
	constexpr uint32_t c_StateOffset  = 16;
	constexpr uint32_t c_ViewOffset   = 32;
	constexpr uint32_t c_VertexOffset = 260;
	constexpr uint32_t c_AutoOffset   = 288;
	constexpr uint32_t c_RigOffset    = c_AutoOffset + 256;
	constexpr uint32_t c_BlobOffset   = c_RigOffset + 128;
	constexpr uint32_t c_PoseOffset   = c_BlobOffset + 32;
	constexpr uint32_t c_PosedOffset  = c_PoseOffset + 16;
	constexpr uint32_t c_FramesOffset = c_PosedOffset + 16;
	constexpr uint32_t c_BufferBytes  = c_FramesOffset + 32;

	constexpr uint32_t c_OutValues = 12;
}

/**
 * A raw buffer loads back the records and the loose attributes the CPU wrote into it.
 *
 * `Load<T>` must see the layout `bgpu_idlgen`'s C++ mirror asserts, or a struct memcpy'd in comes
 * back shuffled -- `CullView` carries the matrix and the fixed array where a target's own packing
 * rules would diverge first. The loose loads are the vertex path's case: a 4-aligned,
 * non-16-aligned address is the only alignment an attribute has.
 *
 * What this deliberately does not cover is a record holding a bindless resource handle. Slang
 * lowers that load to `as_type<texture2d<...>>(ulong)`, which MSL rejects outright, so a
 * handle-bearing payload cannot be raw-loaded on Metal at all. See docs/slang_shaders.md.
 */
TEST_CASE("A raw buffer loads records and loose attributes as written", "[raw][compute][bindless]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	// Every field is distinct, so one read at a neighbour's offset is a wrong value rather than a
	// coincidence. The two integers stay small enough to survive the float the shader reports them
	// as.
	auto state                = bgl::idl::SkinnedTableState();
	state.playback.rig.offset = 7;
	state.playback.clip       = 3;
	state.playback.phase      = 0.25f;
	state.playback.rate       = 1.5f;

	auto view = bgl::idl::CullView();
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			view.viewProj[col][row] = static_cast<float>(col * 4 + row);
		}
	}
	for (uint32_t i = 0; i < 6; ++i)
	{
		const auto f          = static_cast<float>(i);
		view.frustumPlanes[i] = glm::vec4(f, f + 0.25f, f + 0.5f, f + 0.75f);
	}
	// The scalars trailing the float4 are where MSL's 16-byte vector alignment would show.
	view.cameraPosAndPixelsPerUnit = glm::vec4(31.0f, 32.0f, 33.0f, 34.0f);
	view.lodPixelScale             = 1.5f;
	view.lodForcedLevel            = 3u;
	view.lodFadeStep               = 0.25f;
	view.posePixels                = 96.0f;
	view.poseBudget                = 17u;
	view.poseForced                = bgl::idl::cPoseForceTable;

	// The automatic record's last slot and the field after the array, where a slot stride the two
	// sides disagree on would land.
	auto autoState               = bgl::idl::SkinnedAutoState();
	autoState.rig.offset         = 9;
	autoState.slots[3].nodeIndex = 5;
	autoState.slots[3].paramEnd  = 2.5f;
	autoState.footIK.offsetStart = 13;
	autoState.slots[0].weight1   = 0.75f;

	// The rig's last two ranges, the blob entry's trailing leg and the pose slice: the fields the
	// contract added, at the ends of their structs where a misplaced one shows.
	auto rig                      = bgl::idl::Rig();
	rig.boneAnimTable.offsetStart = 21;
	rig.tableSoles.offsetStart    = 22;

	auto blob = bgl::idl::BlobShadow();
	blob.mesh = 4;
	blob.lift = 0.5f;
	blob.leg  = 3;

	auto pose                = bgl::idl::InstancePose();
	pose.palette.offsetStart = 23;

	auto posed                = bgl::idl::AutoPosedInstance();
	posed.meshInstanceIndex   = 31;
	posed.footIK.offsetStart  = 32;
	posed.palette.offsetStart = 33;
	posed.ikScale             = 0.25f;

	auto dominant            = bgl::idl::DominantFrames();
	dominant.lowerFrame      = 41;
	dominant.upperFrame      = 42;
	dominant.upperWeight     = 0.5f;
	dominant.prevLowerFrame  = 43;
	dominant.prevUpperFrame  = 44;
	dominant.prevUpperWeight = 0.25f;

	const auto vertexVec4 = glm::vec4(11.0f, 12.0f, 13.0f, 14.0f);
	const auto vertexVec3 = glm::vec3(21.0f, 22.0f, 23.0f);

	static_assert(c_StateOffset + sizeof(bgl::idl::SkinnedTableState) <= c_ViewOffset);
	static_assert(c_ViewOffset + sizeof(bgl::idl::CullView) <= c_VertexOffset);
	static_assert(c_VertexOffset % 16 != 0, "the loose loads must not be 16-byte aligned");
	static_assert(c_VertexOffset % 4 == 0);
	static_assert(c_AutoOffset + sizeof(bgl::idl::SkinnedAutoState) <= c_RigOffset);
	static_assert(c_RigOffset + sizeof(bgl::idl::Rig) <= c_BlobOffset);
	static_assert(c_BlobOffset + sizeof(bgl::idl::BlobShadow) <= c_PoseOffset);
	static_assert(c_PoseOffset + sizeof(bgl::idl::InstancePose) <= c_PosedOffset);
	static_assert(c_PosedOffset + sizeof(bgl::idl::AutoPosedInstance) <= c_FramesOffset);
	static_assert(c_FramesOffset + sizeof(bgl::idl::DominantFrames) <= c_BufferBytes);

	std::array<std::byte, c_BufferBytes> bytes{};
	std::memcpy(bytes.data() + c_StateOffset, &state, sizeof(state));
	std::memcpy(bytes.data() + c_ViewOffset, &view, sizeof(view));
	std::memcpy(bytes.data() + c_VertexOffset, &vertexVec4, sizeof(vertexVec4));
	std::memcpy(bytes.data() + c_VertexOffset + 16, &vertexVec3, sizeof(vertexVec3));
	std::memcpy(bytes.data() + c_AutoOffset, &autoState, sizeof(autoState));
	std::memcpy(bytes.data() + c_RigOffset, &rig, sizeof(rig));
	std::memcpy(bytes.data() + c_BlobOffset, &blob, sizeof(blob));
	std::memcpy(bytes.data() + c_PoseOffset, &pose, sizeof(pose));
	std::memcpy(bytes.data() + c_PosedOffset, &posed, sizeof(posed));
	std::memcpy(bytes.data() + c_FramesOffset, &dominant, sizeof(dominant));

	const bgpu::BufferHandle records = resourceManager->CreateRawBuffer(
		bgpu::RawViewDesc().SetByteSize(c_BufferBytes).SetDebugName("Raw Record Arena"));
	REQUIRE(resourceManager->ValidBufferHandle(records));

	auto outDesc         = bgpu::ComputeBufferDesc();
	outDesc.initialCount = c_OutValues;
	outDesc.debugName    = "Raw Load Results";
	outDesc.SetElement<glm::vec4>();
	const bgpu::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outValues));

	// The view a buffer was created with is the one thing a shader cannot ask about, so the
	// descriptor has to answer: bind the wrong wrapper and the reads are undefined, not an error.
	CHECK(resourceManager->GetBufferDesc(records).isRaw);
	CHECK_FALSE(resourceManager->GetBufferDesc(outValues).isRaw);

	auto rbDesc                         = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize                     = c_OutValues * sizeof(glm::vec4);
	rbDesc.debugName                    = "Raw Load Readback";
	const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRawLoad"))
			.SetDebugName("Raw Load"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["records"]      = records;
	kernel["gUniforms"]["outValues"]    = outValues;
	kernel["gUniforms"]["stateOffset"]  = c_StateOffset;
	kernel["gUniforms"]["viewOffset"]   = c_ViewOffset;
	kernel["gUniforms"]["vertexOffset"] = c_VertexOffset;
	kernel["gUniforms"]["autoOffset"]   = c_AutoOffset;
	kernel["gUniforms"]["rigOffset"]    = c_RigOffset;
	kernel["gUniforms"]["blobOffset"]   = c_BlobOffset;
	kernel["gUniforms"]["poseOffset"]   = c_PoseOffset;
	kernel["gUniforms"]["posedOffset"]  = c_PosedOffset;
	kernel["gUniforms"]["framesOffset"] = c_FramesOffset;

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->WriteBuffer(records, bytes.data(), 0, bytes.size());
	cmdList->Barrier(
		records,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource));

	auto computeState   = bgpu::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outValues,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outValues);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* got = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
	REQUIRE(got != nullptr);

	constexpr float c_Margin = 0.001f;

	CHECK(
		got[0].x == Catch::Approx(static_cast<float>(state.playback.rig.offset)).margin(c_Margin));
	CHECK(got[0].y == Catch::Approx(static_cast<float>(state.playback.clip)).margin(c_Margin));
	CHECK(got[0].z == Catch::Approx(state.playback.phase).margin(c_Margin));
	CHECK(got[0].w == Catch::Approx(state.playback.rate).margin(c_Margin));

	CHECK(got[1].x == Catch::Approx(view.frustumPlanes[3].x).margin(c_Margin));
	CHECK(got[1].y == Catch::Approx(view.frustumPlanes[3].y).margin(c_Margin));
	CHECK(got[1].z == Catch::Approx(view.frustumPlanes[3].z).margin(c_Margin));
	CHECK(got[1].w == Catch::Approx(view.frustumPlanes[3].w).margin(c_Margin));

	// Slang subscripts a float4x4 by row and glm by column, so the shader's row 2 is element 2 of
	// each of glm's four columns. Storage is column-major on both sides; only the subscript differs.
	CHECK(got[2].x == Catch::Approx(view.viewProj[0][2]).margin(c_Margin));
	CHECK(got[2].y == Catch::Approx(view.viewProj[1][2]).margin(c_Margin));
	CHECK(got[2].z == Catch::Approx(view.viewProj[2][2]).margin(c_Margin));
	CHECK(got[2].w == Catch::Approx(view.viewProj[3][2]).margin(c_Margin));

	CHECK(got[3].x == Catch::Approx(view.cameraPosAndPixelsPerUnit.x).margin(c_Margin));
	CHECK(got[3].y == Catch::Approx(view.cameraPosAndPixelsPerUnit.y).margin(c_Margin));
	CHECK(got[3].z == Catch::Approx(view.cameraPosAndPixelsPerUnit.z).margin(c_Margin));
	CHECK(got[3].w == Catch::Approx(view.cameraPosAndPixelsPerUnit.w).margin(c_Margin));

	CHECK(got[4].x == Catch::Approx(view.lodPixelScale).margin(c_Margin));
	CHECK(got[4].y == Catch::Approx(static_cast<float>(view.lodForcedLevel)).margin(c_Margin));
	CHECK(got[4].z == Catch::Approx(view.lodFadeStep).margin(c_Margin));
	CHECK(got[4].w == Catch::Approx(view.posePixels).margin(c_Margin));
	CHECK(got[7].x == Catch::Approx(static_cast<float>(view.poseBudget)).margin(c_Margin));
	CHECK(got[7].y == Catch::Approx(static_cast<float>(view.poseForced)).margin(c_Margin));

	CHECK(got[8].x == Catch::Approx(static_cast<float>(autoState.rig.offset)).margin(c_Margin));
	CHECK(
		got[8].y ==
		Catch::Approx(static_cast<float>(autoState.slots[3].nodeIndex)).margin(c_Margin));
	CHECK(got[8].z == Catch::Approx(autoState.slots[3].paramEnd).margin(c_Margin));
	CHECK(
		got[8].w ==
		Catch::Approx(static_cast<float>(autoState.footIK.offsetStart)).margin(c_Margin));

	CHECK(got[9].x == Catch::Approx(static_cast<float>(rig.boneAnimTable.offsetStart)));
	CHECK(got[9].y == Catch::Approx(static_cast<float>(rig.tableSoles.offsetStart)));
	CHECK(got[9].z == Catch::Approx(static_cast<float>(blob.leg)));
	CHECK(got[9].w == Catch::Approx(static_cast<float>(pose.palette.offsetStart)));

	CHECK(got[10].x == Catch::Approx(static_cast<float>(posed.meshInstanceIndex)));
	CHECK(got[10].y == Catch::Approx(static_cast<float>(posed.footIK.offsetStart)));
	CHECK(got[10].z == Catch::Approx(static_cast<float>(posed.palette.offsetStart)));
	CHECK(got[10].w == Catch::Approx(posed.ikScale));

	CHECK(got[11].x == Catch::Approx(static_cast<float>(dominant.lowerFrame)));
	CHECK(got[11].y == Catch::Approx(dominant.upperWeight));
	CHECK(got[11].z == Catch::Approx(static_cast<float>(dominant.prevUpperFrame)));
	CHECK(got[11].w == Catch::Approx(dominant.prevUpperWeight));

	CHECK(got[5].x == Catch::Approx(vertexVec4.x).margin(c_Margin));
	CHECK(got[5].y == Catch::Approx(vertexVec4.y).margin(c_Margin));
	CHECK(got[5].z == Catch::Approx(vertexVec4.z).margin(c_Margin));
	CHECK(got[5].w == Catch::Approx(vertexVec4.w).margin(c_Margin));

	CHECK(got[6].x == Catch::Approx(vertexVec3.x).margin(c_Margin));
	CHECK(got[6].y == Catch::Approx(vertexVec3.y).margin(c_Margin));
	CHECK(got[6].z == Catch::Approx(vertexVec3.z).margin(c_Margin));

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outValues, false);
	resourceManager->DestroyBuffer(records, false);
}

/**
 * A compute shader stores typed values into a raw UAV, and the bytes are there.
 *
 * Nothing in bgl writes a raw buffer yet; GPU skinning to a transient vertex buffer will, and by
 * then the vertex path is raw and has no structured view to fall back on. Whether a bindless
 * RWByteAddressBuffer resolves at all is answered here rather than discovered there.
 */
TEST_CASE("A compute shader stores into a raw buffer", "[raw][compute][bindless]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	constexpr uint32_t c_TargetBytes = 32;

	const bgpu::BufferHandle target = resourceManager->CreateRawBuffer(
		bgpu::RawViewDesc().SetByteSize(c_TargetBytes).SetIsUav().SetDebugName("Raw Store"));
	REQUIRE(resourceManager->ValidBufferHandle(target));
	CHECK(resourceManager->GetBufferDesc(target).isRaw);

	auto rbDesc                         = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize                     = c_TargetBytes;
	rbDesc.debugName                    = "Raw Store Readback";
	const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRawStore"))
			.SetDebugName("Raw Store"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["target"] = target;

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;

	cmdList->Open(cmdQueue, cmdAllocator);
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		target,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, target);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* stored = static_cast<const std::byte*>(resourceManager->MapReadback(rb));
	REQUIRE(stored != nullptr);

	auto     vec4Value = glm::vec4();
	auto     vec3Value = glm::vec3();
	uint32_t uintValue = 0;
	std::memcpy(&vec4Value, stored, sizeof(vec4Value));
	std::memcpy(&uintValue, stored + 16, sizeof(uintValue));
	std::memcpy(&vec3Value, stored + 20, sizeof(vec3Value));

	CHECK(vec4Value.x == Catch::Approx(1.0f));
	CHECK(vec4Value.w == Catch::Approx(4.0f));
	CHECK(uintValue == 0xABCDEF01u);
	CHECK(vec3Value.x == Catch::Approx(5.0f));
	CHECK(vec3Value.z == Catch::Approx(7.0f));

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(target, false);
}
