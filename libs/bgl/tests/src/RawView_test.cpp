#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/CullView.h"
#include "idl/VatState.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

namespace
{
	// Where each thing under test sits. The records are past 0 so a wrong base cannot pass; the
	// loose values are at offsets that are 4-aligned and deliberately not 16-aligned, which is all
	// a vertex attribute is ever promised (an importer packs attributes sequentially by format
	// size, so a tangent lands at 20 in a layout carrying no normal).
	constexpr uint32_t c_StateOffset  = 16;
	constexpr uint32_t c_ViewOffset   = 32;
	constexpr uint32_t c_VertexOffset = 196;
	constexpr uint32_t c_BufferBytes  = 224;

	constexpr uint32_t c_OutValues = 5;
}

/**
 * A raw buffer loads back the records and the loose attributes the CPU wrote into it.
 *
 * `Load<T>` must see the layout `bgl_idlgen`'s C++ mirror asserts, or a struct memcpy'd in comes
 * back shuffled -- `CullView` carries the matrix and the fixed array where a target's own packing
 * rules would diverge first. The loose loads are the vertex path's case: a 4-aligned,
 * non-16-aligned address is the only alignment an attribute has.
 *
 * What this deliberately does not cover is a record holding a bindless resource handle. Slang
 * lowers that load to `as_type<texture2d<...>>(ulong)`, which MSL rejects outright, so a
 * handle-bearing payload cannot be raw-loaded on Metal at all. See
 * docs/plans/byte-address-buffer.md.
 */
TEST_CASE("A raw buffer loads records and loose attributes as written", "[raw][compute][bindless]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// Every field is distinct, so one read at a neighbour's offset is a wrong value rather than a
	// coincidence. The two integers stay small enough to survive the float the shader reports them
	// as.
	auto state        = bgl::idl::VatState();
	state.geom.offset = 7;
	state.clip        = 3;
	state.phase       = 0.25f;
	state.rate        = 1.5f;

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

	const auto vertexVec4 = glm::vec4(11.0f, 12.0f, 13.0f, 14.0f);
	const auto vertexVec3 = glm::vec3(21.0f, 22.0f, 23.0f);

	static_assert(c_StateOffset + sizeof(bgl::idl::VatState) <= c_ViewOffset);
	static_assert(c_ViewOffset + sizeof(bgl::idl::CullView) <= c_VertexOffset);
	static_assert(c_VertexOffset % 16 != 0, "the loose loads must not be 16-byte aligned");
	static_assert(c_VertexOffset % 4 == 0);

	std::array<std::byte, c_BufferBytes> bytes{};
	std::memcpy(bytes.data() + c_StateOffset, &state, sizeof(state));
	std::memcpy(bytes.data() + c_ViewOffset, &view, sizeof(view));
	std::memcpy(bytes.data() + c_VertexOffset, &vertexVec4, sizeof(vertexVec4));
	std::memcpy(bytes.data() + c_VertexOffset + 16, &vertexVec3, sizeof(vertexVec3));

	const bgl::BufferHandle records = resourceManager->CreateRawBuffer(
		bgl::RawViewDesc().SetByteSize(c_BufferBytes).SetDebugName("Raw Record Arena"));
	REQUIRE(resourceManager->ValidBufferHandle(records));

	auto outDesc         = bgl::ComputeBufferDesc();
	outDesc.initialCount = c_OutValues;
	outDesc.debugName    = "Raw Load Results";
	outDesc.SetElement<glm::vec4>();
	const bgl::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outValues));

	// The view a buffer was created with is the one thing a shader cannot ask about, so the
	// descriptor has to answer: bind the wrong wrapper and the reads are undefined, not an error.
	CHECK(resourceManager->GetBufferDesc(records).isRaw);
	CHECK_FALSE(resourceManager->GetBufferDesc(outValues).isRaw);

	auto rbDesc                        = bgl::ReadbackBufferDesc();
	rbDesc.byteSize                    = c_OutValues * sizeof(glm::vec4);
	rbDesc.debugName                   = "Raw Load Readback";
	const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRawLoad"))
			.SetDebugName("Raw Load"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["records"]      = records;
	kernel["gUniforms"]["outValues"]    = outValues;
	kernel["gUniforms"]["stateOffset"]  = c_StateOffset;
	kernel["gUniforms"]["viewOffset"]   = c_ViewOffset;
	kernel["gUniforms"]["vertexOffset"] = c_VertexOffset;

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->WriteBuffer(records, bytes.data(), 0, bytes.size());
	cmdList->Barrier(
		records,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource));

	auto computeState   = bgl::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outValues,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outValues);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* got = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
	REQUIRE(got != nullptr);

	constexpr float c_Margin = 0.001f;

	CHECK(got[0].x == Catch::Approx(static_cast<float>(state.geom.offset)).margin(c_Margin));
	CHECK(got[0].y == Catch::Approx(static_cast<float>(state.clip)).margin(c_Margin));
	CHECK(got[0].z == Catch::Approx(state.phase).margin(c_Margin));
	CHECK(got[0].w == Catch::Approx(state.rate).margin(c_Margin));

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

	CHECK(got[3].x == Catch::Approx(vertexVec4.x).margin(c_Margin));
	CHECK(got[3].y == Catch::Approx(vertexVec4.y).margin(c_Margin));
	CHECK(got[3].z == Catch::Approx(vertexVec4.z).margin(c_Margin));
	CHECK(got[3].w == Catch::Approx(vertexVec4.w).margin(c_Margin));

	CHECK(got[4].x == Catch::Approx(vertexVec3.x).margin(c_Margin));
	CHECK(got[4].y == Catch::Approx(vertexVec3.y).margin(c_Margin));
	CHECK(got[4].z == Catch::Approx(vertexVec3.z).margin(c_Margin));

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
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	constexpr uint32_t c_TargetBytes = 32;

	const bgl::BufferHandle target = resourceManager->CreateRawBuffer(
		bgl::RawViewDesc().SetByteSize(c_TargetBytes).SetIsUav().SetDebugName("Raw Store"));
	REQUIRE(resourceManager->ValidBufferHandle(target));
	CHECK(resourceManager->GetBufferDesc(target).isRaw);

	auto rbDesc                        = bgl::ReadbackBufferDesc();
	rbDesc.byteSize                    = c_TargetBytes;
	rbDesc.debugName                   = "Raw Store Readback";
	const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRawStore"))
			.SetDebugName("Raw Store"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["target"] = target;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	cmdList->Open(cmdQueue, cmdAllocator);
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		target,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

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
