#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/idl/Constants.h>
#include <bgpu/buffer/GrowableGpuBuffer.h>
#include <bgpu/buffer/RangeBuffer.h>
#include <bgpu/buffer/RawBuffer.h>
#include <bgpu/buffer/dirty_blocks.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/idl/RawArena.h>
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
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	// The arena's own kind enum. RawBuffer is templated on it so no caller writes a bare integer
	// into a header.
	enum class TestTag : uint32_t
	{
		kSmall = 1,
		kLarge = 2,
	};

	struct SmallPayload
	{
		uint32_t a;
		uint32_t b;
	};

	struct LargePayload
	{
		float values[8];
	};

	std::span<const std::byte>
	BytesOf(const auto& value)
	{
		return std::as_bytes(std::span(&value, 1));
	}
}

/**
 * The byte arena hands out 16-byte-aligned offsets, never offset 0, and writes a header ahead of
 * every record's payload.
 *
 * A dirty block per element (blockSize = sizeof(RawBlock)) makes the tracking readable: one block
 * per 16 bytes, so an allocation's blocks are exactly the ones it touched.
 */
TEST_CASE("A raw arena allocates records and ranges", "[raw][scene]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto desc             = bgpu::RawBufferDesc();
	desc.initialBytes     = 256;
	desc.nullRecordBytes  = bgpu::idl::cRawPayloadOffset + sizeof(LargePayload);
	desc.uploadBlockBytes = sizeof(bgpu::RawBlock);
	desc.debugName        = "Raw Arena Test";

	auto arena = bgpu::RawBuffer<TestTag>(resourceManager, desc);

	// ADR-4: an arena is capped at what its view addresses, not at what the device could allocate.
	CHECK(arena.GetByteCeiling() == bgpu::c_MaxRawBufferBytes);

	SECTION("the null record owns the head, and nothing else is handed it")
	{
		// 16 header + 32 payload = 3 blocks, so the first record cannot start before byte 48.
		const auto small = arena.AddRecord(TestTag::kSmall, BytesOf(SmallPayload{ 1, 2 }));

		CHECK(small.byteOffset >= desc.nullRecordBytes);
		CHECK_FALSE(small.Null());
		CHECK_FALSE(arena.IsOffsetValid(0));
	}

	SECTION("every offset is on the block grid")
	{
		const auto a = arena.AddRecord(TestTag::kSmall, BytesOf(SmallPayload{ 3, 4 }));
		const auto b = arena.AddRecord(TestTag::kLarge, BytesOf(LargePayload{}));
		const auto c = arena.AddBytes(BytesOf(SmallPayload{ 5, 6 }));

		CHECK(a.byteOffset % bgpu::idl::cRawBlockBytes == 0);
		CHECK(b.byteOffset % bgpu::idl::cRawBlockBytes == 0);
		CHECK(c.byteStart % bgpu::idl::cRawBlockBytes == 0);

		// Distinct allocations never overlap: a's record rounds up to whole blocks, and b starts
		// past all of them.
		constexpr uint32_t c_RecordBlocks =
			((bgpu::idl::cRawPayloadOffset + sizeof(SmallPayload) + bgpu::idl::cRawBlockBytes - 1) /
		     bgpu::idl::cRawBlockBytes);
		CHECK(b.byteOffset >= a.byteOffset + c_RecordBlocks * bgpu::idl::cRawBlockBytes);
	}

	SECTION("a record carries the tag it was written with")
	{
		const auto small = arena.AddRecord(TestTag::kSmall, BytesOf(SmallPayload{ 7, 8 }));
		const auto large = arena.AddRecord(TestTag::kLarge, BytesOf(LargePayload{}));

		CHECK(arena.GetTagAt(small.byteOffset) == TestTag::kSmall);
		CHECK(arena.GetTagAt(large.byteOffset) == TestTag::kLarge);
	}

	SECTION("a range carries no header, so its bytes start where it says")
	{
		const auto payload = SmallPayload{ 0xAAAAAAAA, 0xBBBBBBBB };
		const auto range   = arena.AddBytes(BytesOf(payload));

		CHECK(arena.IsOffsetValid(range.byteStart));

		// No header: the first four bytes are the caller's, not a tag. GetTagAt reads exactly those.
		CHECK(std::to_underlying(arena.GetTagAt(range.byteStart)) == payload.a);
	}

	SECTION("an erased offset stops being valid")
	{
		const auto record = arena.AddRecord(TestTag::kSmall, BytesOf(SmallPayload{ 9, 10 }));
		REQUIRE(arena.IsOffsetValid(record.byteOffset));

		arena.Erase(record.byteOffset);
		CHECK_FALSE(arena.IsOffsetValid(record.byteOffset));
	}

	SECTION("an offset off the grid is never valid")
	{
		const auto record = arena.AddRecord(TestTag::kSmall, BytesOf(SmallPayload{ 11, 12 }));
		CHECK_FALSE(arena.IsOffsetValid(record.byteOffset + 4));
	}

	SECTION("growth preserves the offsets already handed out")
	{
		const auto before = arena.GetByteCapacity();

		std::vector<bgpu::idl::RawEntry> records;
		for (uint32_t i = 0; i < 64; ++i)
		{
			records.push_back(arena.AddRecord(TestTag::kLarge, BytesOf(LargePayload{})));
		}

		CHECK(arena.GetByteCapacity() > before);

		for (const auto record : records)
		{
			CHECK(arena.IsOffsetValid(record.byteOffset));
			CHECK(arena.GetTagAt(record.byteOffset) == TestTag::kLarge);
		}
	}
}

/**
 * Growth past the byte ceiling throws rather than handing out an offset no shader can reach.
 *
 * The ceiling under test is a small one, not the real 2^32: what is being pinned is that the
 * refusal happens at all, and allocating 4 GiB to watch it would be absurd.
 */
TEST_CASE("A range buffer refuses to grow past its byte ceiling", "[raw][scene]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	// 16 elements of 4 bytes plus the reserved null one is a capacity of 17; the ceiling is 18, so
	// there is exactly one element of growth to be had.
	auto desc         = bgpu::RangeBufferDesc();
	desc.initialCount = 16;
	desc.maxBytes     = 18 * sizeof(uint32_t);
	desc.blockSize    = sizeof(uint32_t);
	desc.debugName    = "Capped Range";

	auto capped = bgpu::RangeBuffer<uint32_t>(resourceManager, desc);

	// Fills the initial capacity exactly, so the next allocation is the one that must grow.
	CHECK_NOTHROW(capped.AllocateRange(16));

	// Growth to the ceiling is allowed, and clamps to it rather than overshooting the way the
	// growth curve alone would.
	CHECK_NOTHROW(capped.AllocateRange(1));
	CHECK(capped.Capacity() == 18);

	// Past it the buffer says so, rather than handing back an offset a uint cannot address.
	CHECK_THROWS_AS(capped.AllocateRange(1), std::runtime_error);
}

/**
 * The upload arithmetic holds at the top of the address space.
 *
 * An arena at its byte ceiling is 65536 blocks of 65536 bytes. Computed in 32 bits their product is
 * 2^32, which is 0 -- so the copy was skipped and a fully dirty arena uploaded nothing, a corruption
 * with no error anywhere. Device-free, like the growth curve beside it: the arithmetic is what is
 * under test, and allocating 4 GiB to watch it would be absurd.
 */
TEST_CASE("The copy slice at the top of the address space does not wrap", "[raw][scene]")
{
	constexpr uint32_t c_BlockSize = 65536;
	constexpr uint32_t c_Blocks    = 65536;

	const auto whole = bgpu::MakeCopySlice(0, c_Blocks, c_BlockSize, bgpu::c_MaxRawBufferBytes);

	CHECK(whole.offset == 0);
	CHECK(whole.size == bgpu::c_MaxRawBufferBytes);

	// The last block of that arena, which a 32-bit offset also cannot express.
	const auto tail =
		bgpu::MakeCopySlice(c_Blocks - 1, c_Blocks, c_BlockSize, bgpu::c_MaxRawBufferBytes);

	CHECK(tail.offset == bgpu::c_MaxRawBufferBytes - c_BlockSize);
	CHECK(tail.size == c_BlockSize);

	// A run past the end of the mirror uploads nothing rather than a negative length.
	CHECK(bgpu::MakeCopySlice(4, 8, c_BlockSize, 2 * c_BlockSize) == bgpu::CopySlice{});

	// And a partial tail is clamped to what the mirror holds.
	CHECK(bgpu::MakeCopySlice(1, 4, 16, 40) == bgpu::CopySlice{ 16, 24 });

	// The blocks a range lands in are computed in the same width, so the last elements of a full
	// arena mark the last block rather than one below the first.
	constexpr uint32_t c_LastPair =
		static_cast<uint32_t>((bgpu::c_MaxRawBufferBytes / bgpu::idl::cRawBlockBytes) - 2);

	CHECK(
		bgpu::FindDirtyBlocks(c_LastPair, 2, bgpu::idl::cRawBlockBytes, c_BlockSize) ==
		bgpu::DirtyBlockSpan{ c_Blocks - 1, c_Blocks - 1 });
	CHECK(bgpu::FindDirtyBlocks(4, 3, 16, 16) == bgpu::DirtyBlockSpan{ 4, 6 });
}

/**
 * The byte ceiling is arithmetic, so it can be checked at its real value rather than a stand-in.
 *
 * A block index reaches 2^28 before its byte offset reaches 2^32, which is where an arena stops
 * being addressable however much of it a device would allocate.
 */
TEST_CASE("Growth stops at what a raw view can address", "[raw][scene]")
{
	constexpr uint64_t c_Block     = bgpu::idl::cRawBlockBytes;
	constexpr uint32_t c_LastBlock = static_cast<uint32_t>(bgpu::c_MaxRawBufferBytes / c_Block);

	// Exactly at the ceiling is allowed: a uint addresses 0 .. 2^32-1, so the last byte is reachable.
	CHECK(
		bgpu::GrowCapacityFor(c_LastBlock - 1, 1, c_Block, bgpu::c_MaxRawBufferBytes) ==
		c_LastBlock);

	// One block past it is refused rather than wrapped.
	CHECK(bgpu::GrowCapacityFor(c_LastBlock, 1, c_Block, bgpu::c_MaxRawBufferBytes) == 0);

	// Under a ceiling the growth curve is clamped to it rather than overshooting.
	CHECK(bgpu::GrowCapacityFor(17, 1, 4, 72) == 18);
	CHECK(bgpu::GrowCapacityFor(18, 1, 4, 72) == 0);

	// With no ceiling the curve is untouched.
	CHECK(bgpu::GrowCapacityFor(17, 1, 4, 0) == bgpu::NextGpuBufferCapacity(17, 18, 4));
}

/**
 * A shader reads back the records and the range the arena wrote.
 *
 * The CPU's idea of where a header ends and a payload begins is ADR-7's layout, and until something
 * reads one across the seam nothing checks that the shader agrees. This is also what compiles
 * `RawBuffer`'s accessors at all -- a Slang generic method is type-checked only where it is
 * instantiated.
 */
TEST_CASE("A shader reads the records a raw arena wrote", "[raw][compute][scene]")
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

	auto desc            = bgpu::RawBufferDesc();
	desc.initialBytes    = 256;
	desc.nullRecordBytes = bgpu::idl::cRawPayloadOffset + sizeof(glm::vec4);
	desc.debugName       = "Raw Arena Read";

	auto arena = bgpu::RawBuffer<TestTag>(resourceManager, desc);

	const auto payloadA = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
	const auto payloadB = glm::vec4(5.0f, 6.0f, 7.0f, 8.0f);
	const auto ranged   = glm::vec4(9.0f, 10.0f, 11.0f, 12.0f);

	const auto recordA = arena.AddRecord(TestTag::kSmall, BytesOf(payloadA));
	const auto recordB = arena.AddRecord(TestTag::kLarge, BytesOf(payloadB));
	const auto range   = arena.AddBytes(BytesOf(ranged));

	constexpr uint32_t c_Results = 4;

	auto outDesc         = bgpu::ComputeBufferDesc();
	outDesc.initialCount = c_Results;
	outDesc.debugName    = "Arena Read Results";
	outDesc.SetElement<glm::vec4>();
	const bgpu::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);

	auto rbDesc                         = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize                     = c_Results * sizeof(glm::vec4);
	rbDesc.debugName                    = "Arena Read Readback";
	const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSRawBufferRead"))
			.SetDebugName("Raw Arena Read"));
	REQUIRE(kernel.pipeline != nullptr);

	cmdList->Open(cmdQueue, cmdAllocator);

	// The arena is only bytes on the CPU until this runs.
	arena.Update(cmdList);

	kernel["gUniforms"]["arena"]     = arena.GetBufferHandle();
	kernel["gUniforms"]["outValues"] = outValues;
	kernel["gUniforms"]["recordA"]   = recordA.byteOffset;
	kernel["gUniforms"]["recordB"]   = recordB.byteOffset;
	kernel["gUniforms"]["range"]     = range.byteStart;

	cmdList->Barrier(
		arena.GetBufferHandle(),
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

	CHECK(got[0].x == Catch::Approx(payloadA.x));
	CHECK(got[0].w == Catch::Approx(payloadA.w));
	CHECK(got[1].x == Catch::Approx(payloadB.x));
	CHECK(got[1].w == Catch::Approx(payloadB.w));

	// The tags the records were written with, read out of their headers.
	CHECK(got[2].x == Catch::Approx(static_cast<float>(TestTag::kSmall)));
	CHECK(got[2].y == Catch::Approx(static_cast<float>(TestTag::kLarge)));

	// A range has no header, so its bytes start where it says rather than a payload offset later.
	CHECK(got[3].x == Catch::Approx(ranged.x));
	CHECK(got[3].w == Catch::Approx(ranged.w));

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outValues, false);
}
