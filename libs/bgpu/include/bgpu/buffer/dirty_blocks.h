#pragma once
#include <algorithm>
#include <bgpu/buffer/GrowableGpuBuffer.h>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace bgpu
{
	// The first and last dirty block a range of elements touches, both inclusive.
	struct DirtyBlockSpan
	{
		uint32_t first = 0;
		uint32_t last  = 0;

		bool
		operator==(const DirtyBlockSpan&) const noexcept = default;
	};

	// The byte window one run of dirty blocks uploads, clamped to what the mirror actually holds.
	struct CopySlice
	{
		uint64_t offset = 0;
		uint64_t size   = 0;

		bool
		operator==(const CopySlice&) const noexcept = default;
	};

	/**
	 * The bytes to upload for dirty blocks `[startBlk, endBlk)`, clamped to `totalBytes`.
	 *
	 * 64-bit because a 2^32-byte mirror is 65536 blocks of 65536 bytes, a product that is 0 in 32.
	 *
	 * @post a slice of zero size where the run starts past the end of the mirror.
	 */
	[[nodiscard]] constexpr CopySlice
	MakeCopySlice(
		uint32_t startBlk,
		uint32_t endBlk,
		uint32_t blockSize,
		uint64_t totalBytes) noexcept
	{
		const uint64_t offset = static_cast<uint64_t>(startBlk) * blockSize;
		if (offset >= totalBytes)
		{
			return {};
		}

		const uint64_t size = static_cast<uint64_t>(endBlk - startBlk) * blockSize;
		return { offset, std::min(size, totalBytes - offset) };
	}

	/**
	 * The capacity a growth of `count` more elements should take a buffer to, clamped to a byte
	 * ceiling (`maxBytes` of 0 leaves it bounded only by device memory).
	 *
	 * A free function so the ceiling can be tested at the real 2^32 without allocating it.
	 *
	 * @post 0 when the request itself crosses the ceiling -- the caller has the name and the numbers
	 * to report it with.
	 */
	[[nodiscard]] inline uint32_t
	GrowCapacityFor(
		uint32_t current,
		uint32_t count,
		uint64_t elementSize,
		uint64_t maxBytes) noexcept
	{
		const uint64_t requiredBytes = (static_cast<uint64_t>(current) + count) * elementSize;

		if (maxBytes != 0 && requiredBytes > maxBytes)
		{
			return 0;
		}

		const uint32_t grown =
			NextGpuBufferCapacity(current, current + count, static_cast<uint32_t>(elementSize));

		// The growth curve overshoots on purpose; the ceiling is not a budget to overshoot past.
		return maxBytes == 0 ?
		           grown :
		           static_cast<uint32_t>(std::min<uint64_t>(grown, maxBytes / elementSize));
	}

	/**
	 * Which blocks a range of `count` elements starting at `startIdx` lands in.
	 *
	 * 64-bit for the same reason as MakeCopySlice: an element past the first 2^32 bytes would
	 * otherwise mark a block near the front.
	 *
	 * @pre count is non-zero and blockSize is non-zero.
	 */
	[[nodiscard]] constexpr DirtyBlockSpan
	FindDirtyBlocks(
		uint32_t startIdx,
		uint32_t count,
		uint64_t elementSize,
		uint32_t blockSize) noexcept
	{
		const uint64_t startOffsetBytes = static_cast<uint64_t>(startIdx) * elementSize;
		const uint64_t endOffsetBytes =
			((static_cast<uint64_t>(startIdx) + count) * elementSize) - 1;

		return { static_cast<uint32_t>(startOffsetBytes / blockSize),
			     static_cast<uint32_t>(endOffsetBytes / blockSize) };
	}

	/**
	 * Hands `copy` the byte window of every run of consecutive dirty blocks, in order, skipping
	 * the runs that lie wholly past the `elementCount` elements the mirror holds.
	 *
	 * Takes the element count rather than the mirror's byte size so the product is formed here, in
	 * 64 bits: a mirror of exactly 2^32 bytes is 0 in 32, and every run would be skipped.
	 */
	template <std::invocable<const CopySlice&> F>
	void
	ForEachDirtySlice(
		const std::vector<bool>& dirtyBlocks,
		uint32_t                 blockSize,
		uint64_t                 elementCount,
		uint64_t                 elementSize,
		F&&                      copy) noexcept(std::is_nothrow_invocable_v<F&, const CopySlice&>)
	{
		const uint64_t totalBytes = elementCount * elementSize;
		const auto     blockCount = static_cast<uint32_t>(dirtyBlocks.size());

		uint32_t block = 0;
		while (block < blockCount)
		{
			if (!dirtyBlocks[block])
			{
				++block;
				continue;
			}

			const uint32_t runStart = block;
			while (block < blockCount && dirtyBlocks[block])
			{
				++block;
			}

			const CopySlice slice = MakeCopySlice(runStart, block, blockSize, totalBytes);
			if (slice.size > 0)
			{
				copy(slice);
			}
		}
	}
}
