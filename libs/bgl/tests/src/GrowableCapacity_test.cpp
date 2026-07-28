#include "idl/Constants.h"
#include "scene/GrowableGpuBuffer.h"
#include "types/SubmeshInstance.h"
#include <catch2/catch_test_macros.hpp>

// The growth curve doubles until a buffer passes 64 MiB, then tapers to 1.5x + 1 so the transient
// old+new residency of a doubling does not cost a second full copy. Doubling preserves any power-of-
// two alignment the initial capacity had; the taper does not, and the first tapered capacity for
// SubmeshInstance is 6291457 -- one element past a multiple of 256.
//
// SceneView pads its instance buffer up to a multiple of cHistogramGroupSize every Update, and the
// counting sort dispatches whole groups over that padded range. An unaligned capacity there puts the
// padding write, and the reads that follow it, past the end of the allocation. PackedBuffer's
// capacityAlignment is what keeps that from happening; these pin the arithmetic it has to survive.
TEST_CASE("Growable capacities taper past 64 MiB", "[scene][capacity]")
{
	constexpr uint32_t c_Stride = sizeof(bgl::SubmeshInstance);
	static_assert(c_Stride == 16, "The taper threshold below is computed from this stride");

	SECTION("doubling holds below the taper")
	{
		CHECK(bgl::NextGpuBufferCapacity(256, 257, c_Stride) == 512);
		CHECK(bgl::NextGpuBufferCapacity(512, 513, c_Stride) == 1024);
		CHECK(bgl::NextGpuBufferCapacity(2097152, 2097153, c_Stride) == 4194304);
	}

	SECTION("the taper breaks the alignment doubling preserved")
	{
		// 4194304 * 16 B is exactly 64 MiB, so this is the first step to taper.
		const uint32_t tapered = bgl::NextGpuBufferCapacity(4194304, 4194305, c_Stride);

		CHECK(tapered == 6291457);
		CHECK(tapered % bgl::idl::cHistogramGroupSize != 0);
	}
}

// What the fix guarantees: whatever the growth curve returns, an aligned PackedBuffer rounds it up,
// so round_up(count, cHistogramGroupSize) can never exceed the capacity that backs it.
TEST_CASE("Aligning a tapered capacity covers the padded range", "[scene][capacity]")
{
	constexpr uint32_t c_Stride = sizeof(bgl::SubmeshInstance);
	constexpr uint32_t c_Align  = bgl::idl::cHistogramGroupSize;

	uint32_t capacity = c_Align;
	for (int step = 0; step < 24; ++step)
	{
		capacity =
			core::round_up(bgl::NextGpuBufferCapacity(capacity, capacity + 1, c_Stride), c_Align);

		INFO("step " << step << " capacity " << capacity);
		REQUIRE(capacity % c_Align == 0);

		// The live count can reach capacity, and Update pads from there to the next group boundary.
		REQUIRE(core::round_up(capacity, c_Align) <= capacity);
	}

	// Far enough to have crossed the taper, or the loop proved nothing about it.
	REQUIRE(static_cast<uint64_t>(capacity) * c_Stride > 64ull * 1024 * 1024);
}
