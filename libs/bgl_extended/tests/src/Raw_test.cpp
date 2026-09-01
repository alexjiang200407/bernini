#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/RawBuffer.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

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
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto desc             = bgl::RawBufferDesc();
	desc.initialBytes     = 256;
	desc.nullRecordBytes  = bgl::idl::cRawPayloadOffset + sizeof(LargePayload);
	desc.uploadBlockBytes = sizeof(bgl::RawBlock);
	desc.debugName        = "Raw Arena Test";

	auto arena = bgl::RawBuffer<TestTag>(desc, resourceManager);
	REQUIRE(arena.IsInitialized());

	// ADR-4: an arena is capped at what its view addresses, not at what the device could allocate.
	CHECK(arena.GetByteCeiling() == bgl::c_MaxRawBufferBytes);

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

		CHECK(a.byteOffset % bgl::idl::cRawBlockBytes == 0);
		CHECK(b.byteOffset % bgl::idl::cRawBlockBytes == 0);
		CHECK(c.byteStart % bgl::idl::cRawBlockBytes == 0);

		// Distinct allocations never overlap: a's record rounds up to whole blocks, and b starts
		// past all of them.
		constexpr uint32_t c_RecordBlocks =
			((bgl::idl::cRawPayloadOffset + sizeof(SmallPayload) + bgl::idl::cRawBlockBytes - 1) /
		     bgl::idl::cRawBlockBytes);
		CHECK(b.byteOffset >= a.byteOffset + c_RecordBlocks * bgl::idl::cRawBlockBytes);
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
		CHECK(static_cast<uint32_t>(arena.GetTagAt(range.byteStart)) == payload.a);
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

		std::vector<bgl::idl::RawEntry> records;
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

	arena.Release(false);
}

/**
 * Growth past the byte ceiling throws rather than handing out an offset no shader can reach.
 *
 * The ceiling under test is a small one, not the real 2^32: what is being pinned is that the
 * refusal happens at all, and allocating 4 GiB to watch it would be absurd.
 */
TEST_CASE("A range buffer refuses to grow past its byte ceiling", "[raw][scene]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	// 16 elements of 4 bytes plus the reserved null one is a capacity of 17; the ceiling is 18, so
	// there is exactly one element of growth to be had.
	auto desc         = bgl::RangeBufferDesc();
	desc.initialCount = 16;
	desc.maxBytes     = 18 * sizeof(uint32_t);
	desc.blockSize    = sizeof(uint32_t);
	desc.debugName    = "Capped Range";

	auto capped = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

	// Fills the initial capacity exactly, so the next allocation is the one that must grow.
	CHECK_NOTHROW(capped.AllocateRange(16));

	// Growth to the ceiling is allowed, and clamps to it rather than overshooting the way the
	// growth curve alone would.
	CHECK_NOTHROW(capped.AllocateRange(1));
	CHECK(capped.Capacity() == 18);

	// Past it the buffer says so, rather than handing back an offset a uint cannot address.
	CHECK_THROWS_AS(capped.AllocateRange(1), std::runtime_error);

	capped.Release(false);
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

	const auto whole = bgl::MakeCopySlice(0, c_Blocks, c_BlockSize, bgl::c_MaxRawBufferBytes);

	CHECK(whole.offset == 0);
	CHECK(whole.size == bgl::c_MaxRawBufferBytes);

	// The last block of that arena, which a 32-bit offset also cannot express.
	const auto tail =
		bgl::MakeCopySlice(c_Blocks - 1, c_Blocks, c_BlockSize, bgl::c_MaxRawBufferBytes);

	CHECK(tail.offset == bgl::c_MaxRawBufferBytes - c_BlockSize);
	CHECK(tail.size == c_BlockSize);

	// A run past the end of the mirror uploads nothing rather than a negative length.
	CHECK(bgl::MakeCopySlice(4, 8, c_BlockSize, 2 * c_BlockSize) == bgl::CopySlice{});

	// And a partial tail is clamped to what the mirror holds.
	CHECK(bgl::MakeCopySlice(1, 4, 16, 40) == bgl::CopySlice{ 16, 24 });

	// The blocks a range lands in are computed in the same width, so the last elements of a full
	// arena mark the last block rather than one below the first.
	constexpr uint32_t c_LastPair =
		static_cast<uint32_t>((bgl::c_MaxRawBufferBytes / bgl::idl::cRawBlockBytes) - 2);

	CHECK(
		bgl::FindDirtyBlocks(c_LastPair, 2, bgl::idl::cRawBlockBytes, c_BlockSize) ==
		bgl::DirtyBlockSpan{ c_Blocks - 1, c_Blocks - 1 });
	CHECK(bgl::FindDirtyBlocks(4, 3, 16, 16) == bgl::DirtyBlockSpan{ 4, 6 });
}

/**
 * The byte ceiling is arithmetic, so it can be checked at its real value rather than a stand-in.
 *
 * A block index reaches 2^28 before its byte offset reaches 2^32, which is where an arena stops
 * being addressable however much of it a device would allocate.
 */
TEST_CASE("Growth stops at what a raw view can address", "[raw][scene]")
{
	constexpr uint64_t c_Block     = bgl::idl::cRawBlockBytes;
	constexpr uint32_t c_LastBlock = static_cast<uint32_t>(bgl::c_MaxRawBufferBytes / c_Block);

	// Exactly at the ceiling is allowed: a uint addresses 0 .. 2^32-1, so the last byte is reachable.
	CHECK(
		bgl::GrowCapacityFor(c_LastBlock - 1, 1, c_Block, bgl::c_MaxRawBufferBytes) == c_LastBlock);

	// One block past it is refused rather than wrapped.
	CHECK(bgl::GrowCapacityFor(c_LastBlock, 1, c_Block, bgl::c_MaxRawBufferBytes) == 0);

	// Under a ceiling the growth curve is clamped to it rather than overshooting.
	CHECK(bgl::GrowCapacityFor(17, 1, 4, 72) == 18);
	CHECK(bgl::GrowCapacityFor(18, 1, 4, 72) == 0);

	// With no ceiling the curve is untouched.
	CHECK(bgl::GrowCapacityFor(17, 1, 4, 0) == bgl::NextGpuBufferCapacity(17, 18, 4));
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

	auto desc            = bgl::RawBufferDesc();
	desc.initialBytes    = 256;
	desc.nullRecordBytes = bgl::idl::cRawPayloadOffset + sizeof(glm::vec4);
	desc.debugName       = "Raw Arena Read";

	auto arena = bgl::RawBuffer<TestTag>(desc, resourceManager);

	const auto payloadA = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
	const auto payloadB = glm::vec4(5.0f, 6.0f, 7.0f, 8.0f);
	const auto ranged   = glm::vec4(9.0f, 10.0f, 11.0f, 12.0f);

	const auto recordA = arena.AddRecord(TestTag::kSmall, BytesOf(payloadA));
	const auto recordB = arena.AddRecord(TestTag::kLarge, BytesOf(payloadB));
	const auto range   = arena.AddBytes(BytesOf(ranged));

	constexpr uint32_t c_Results = 4;

	auto outDesc         = bgl::ComputeBufferDesc();
	outDesc.initialCount = c_Results;
	outDesc.debugName    = "Arena Read Results";
	outDesc.SetElement<glm::vec4>();
	const bgl::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);

	auto rbDesc                        = bgl::ReadbackBufferDesc();
	rbDesc.byteSize                    = c_Results * sizeof(glm::vec4);
	rbDesc.debugName                   = "Arena Read Readback";
	const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
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
	arena.Release(false);
}
