#include <bgpu/buffer/dirty_blocks.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

namespace
{
	std::vector<bgpu::CopySlice>
	DirtySlices(
		const std::vector<bool>& dirtyBlocks,
		uint32_t                 blockSize,
		uint64_t                 elementCount,
		uint64_t                 elementSize)
	{
		auto slices = std::vector<bgpu::CopySlice>();
		bgpu::ForEachDirtySlice(
			dirtyBlocks,
			blockSize,
			elementCount,
			elementSize,
			[&](const bgpu::CopySlice& slice) { slices.push_back(slice); });
		return slices;
	}

	// An EntryBuffer's shape: one element per entry and the desc's default block, with no byte
	// ceiling -- unlike a raw RangeBuffer, nothing stops it at 2^32.
	constexpr uint32_t c_BlockSize   = 65536;
	constexpr uint64_t c_EntrySize   = 64;
	constexpr uint64_t c_FourGiB     = uint64_t(1) << 32;
	constexpr uint64_t c_EntriesAt4G = c_FourGiB / c_EntrySize;
	constexpr uint32_t c_BlocksAt4G  = static_cast<uint32_t>(c_FourGiB / c_BlockSize);
}

/**
 * A fully dirty mirror of exactly 2^32 bytes uploads all of it.
 *
 * EntryBuffer and PackedBuffer once took the mirror's byte size as a uint32_t, which is 0 here, so
 * every run started "past the end" and nothing uploaded -- the wrap MakeCopySlice fixed for
 * RangeBuffer. Device-free: the arithmetic is what is under test.
 */
TEST_CASE("A fully dirty 4 GiB entry mirror uploads every byte", "[buffer][dirty]")
{
	const auto slices =
		DirtySlices(std::vector<bool>(c_BlocksAt4G, true), c_BlockSize, c_EntriesAt4G, c_EntrySize);

	REQUIRE(slices.size() == 1);
	CHECK(slices[0] == bgpu::CopySlice{ 0, c_FourGiB });
}

TEST_CASE("An entry past the first 4 GiB marks and uploads its own block", "[buffer][dirty]")
{
	// In 32 bits its byte offset wrapped to 0, marking and uploading the first block instead.
	const auto index = static_cast<uint32_t>(c_EntriesAt4G);

	CHECK(
		bgpu::FindDirtyBlocks(index, 1, c_EntrySize, c_BlockSize) ==
		bgpu::DirtyBlockSpan{ c_BlocksAt4G, c_BlocksAt4G });

	auto dirty          = std::vector<bool>(c_BlocksAt4G + 1, false);
	dirty[c_BlocksAt4G] = true;

	const auto slices = DirtySlices(dirty, c_BlockSize, c_EntriesAt4G + 1, c_EntrySize);

	REQUIRE(slices.size() == 1);
	CHECK(slices[0] == bgpu::CopySlice{ c_FourGiB, c_EntrySize });
}

TEST_CASE("Each run of dirty blocks is one upload, clamped to the mirror", "[buffer][dirty]")
{
	// Blocks of 16 bytes over 5 elements of 8: 40 bytes, so block 2 is half full and block 3 empty.
	const auto dirty = std::vector<bool>{ true, false, true, true, false, true };

	const auto slices = DirtySlices(dirty, 16, 5, 8);

	// Block 5 lies wholly past the mirror and uploads nothing.
	REQUIRE(slices.size() == 2);
	CHECK(slices[0] == bgpu::CopySlice{ 0, 16 });
	CHECK(slices[1] == bgpu::CopySlice{ 32, 8 });
}

TEST_CASE("A clean mirror uploads nothing", "[buffer][dirty]")
{
	CHECK(DirtySlices(std::vector<bool>(8, false), 16, 8, 16).empty());
	CHECK(DirtySlices({}, 16, 0, 16).empty());
}
