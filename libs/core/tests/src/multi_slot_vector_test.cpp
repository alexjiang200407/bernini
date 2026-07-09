#include "TrackedElement.h"
#include <core/containers/multi_slot_vector.h>

using test::TrackedElement;

TEST_CASE("multi_slot_vector allocates a contiguous active range", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(8);
	REQUIRE(slots.size() == 8u);

	auto handle = slots.allocate_slots(3u);
	REQUIRE(handle.index == 0u);
	REQUIRE(handle.count == 3u);
	REQUIRE(slots.is_allocated_root(handle.index));
	REQUIRE(slots.handle_at(0u) == handle);

	for (uint32_t i = 0; i < 3u; ++i)
	{
		REQUIRE(slots.valid(i));
	}
	REQUIRE_FALSE(slots.valid(3u));

	// Only the first slot of the range is a root.
	REQUIRE_FALSE(slots.is_allocated_root(1u));
	REQUIRE_THROWS_AS(slots.handle_at(1u), std::runtime_error);
}

TEST_CASE("multi_slot_vector rejects empty and oversized allocations", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(2);

	REQUIRE_THROWS_AS(slots.allocate_slots(0u), std::runtime_error);
	REQUIRE_THROWS_AS(slots.allocate_slots(3u), std::runtime_error);

	(void)slots.allocate_slots(2u);
	REQUIRE_THROWS_AS(slots.allocate_slots(1u), std::runtime_error);
}

TEST_CASE("multi_slot_vector grows without a slot limit", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots;

	auto a = slots.allocate_slots(3u);
	auto b = slots.allocate_slots(2u);
	REQUIRE(a.index == 0u);
	REQUIRE(b.index == 3u);
	REQUIRE(slots.size() == 5u);
}

TEST_CASE("multi_slot_vector splits a free segment first-fit", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(6);

	auto a = slots.allocate_slots(2u);
	auto b = slots.allocate_slots(2u);
	auto c = slots.allocate_slots(2u);
	REQUIRE(a.index == 0u);
	REQUIRE(b.index == 2u);
	REQUIRE(c.index == 4u);
}

TEST_CASE("multi_slot_vector coalesces adjacent freed segments", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(6);

	auto a = slots.allocate_slots(2u);
	auto b = slots.allocate_slots(2u);
	auto c = slots.allocate_slots(2u);

	// Freeing the two neighbours must merge into one 4-wide hole, not two 2-wide
	// ones -- otherwise this allocation cannot be satisfied.
	slots.erase(b);
	slots.erase(c);
	auto merged = slots.allocate_slots(4u);
	REQUIRE(merged.index == 2u);

	// And merging backwards over `a` gives the whole vector back.
	slots.erase(merged);
	slots.erase(a);
	auto whole = slots.allocate_slots(6u);
	REQUIRE(whole.index == 0u);
}

TEST_CASE("multi_slot_vector invalidates handles to an erased range", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(4);

	auto stale = slots.allocate_slots(4u);
	REQUIRE(slots.valid(stale.index, stale.generation));

	slots.erase(stale);
	REQUIRE_FALSE(slots.valid(stale.index, stale.generation));
	REQUIRE_FALSE(slots.valid(0u));
	REQUIRE_FALSE(slots.is_allocated_root(0u));
	REQUIRE_THROWS_AS(slots.erase(stale), std::runtime_error);

	auto fresh = slots.allocate_slots(4u);
	REQUIRE(fresh.index == stale.index);
	REQUIRE(fresh.generation != stale.generation);
	REQUIRE(slots.valid(fresh.index, fresh.generation));
	REQUIRE_FALSE(slots.valid(stale.index, stale.generation));
}

TEST_CASE("multi_slot_vector cannot erase from a non-root index", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(4);

	auto handle    = slots.allocate_slots(3u);
	auto interior  = handle;
	interior.index = 1u;

	REQUIRE_THROWS_AS(slots.erase(interior), std::runtime_error);
	REQUIRE(slots.valid(handle.index, handle.generation));
}

// The generation is bumped on the root only. This pins the property that makes
// that sound: a handle is only ever minted at a root, so a slot that has never
// rooted an allocation cannot collide with any handle a caller still holds.
TEST_CASE(
	"multi_slot_vector keeps stale handles dead when a range is resplit",
	"[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(6);

	auto wide = slots.allocate_slots(6u);
	slots.erase(wide);

	// The freed span is handed back out as two ranges. Slot 0 rooted `wide`, so it
	// must come back at a higher generation; slot 3 never rooted anything.
	auto lo = slots.allocate_slots(3u);
	auto hi = slots.allocate_slots(3u);
	REQUIRE(lo.index == 0u);
	REQUIRE(hi.index == 3u);

	REQUIRE_FALSE(slots.valid(wide.index, wide.generation));
	REQUIRE(lo.generation != wide.generation);
	REQUIRE(slots.valid(lo.index, lo.generation));
	REQUIRE(slots.valid(hi.index, hi.generation));

	slots[0u] = 10;
	slots[3u] = 30;
	REQUIRE(slots[0u] == 10);
	REQUIRE(slots[3u] == 30);

	// Re-rooting slot 3 must also retire its own handle.
	slots.erase(hi);
	auto hi2 = slots.allocate_slots(3u);
	REQUIRE(hi2.index == hi.index);
	REQUIRE(hi2.generation != hi.generation);
	REQUIRE_FALSE(slots.valid(hi.index, hi.generation));
	REQUIRE(slots.valid(hi2.index, hi2.generation));
}

// allocate_slots does not clear the range it hands out, which is only correct
// because erase() leaves every slot default-constructed.
TEST_CASE("multi_slot_vector erase leaves slots default-constructed", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(4);

	auto first = slots.allocate_slots(3u);
	slots[0u]  = 1;
	slots[1u]  = 2;
	slots[2u]  = 3;
	slots.erase(first);

	auto second = slots.allocate_slots(3u);
	REQUIRE(second.index == first.index);
	REQUIRE(slots[0u] == 0);
	REQUIRE(slots[1u] == 0);
	REQUIRE(slots[2u] == 0);
}

TEST_CASE("multi_slot_vector rejects out-of-bounds and inactive access", "[multi_slot_vector]")
{
	core::multi_slot_vector<int> slots(4);

	REQUIRE_THROWS_AS(slots[0u], std::runtime_error);
	REQUIRE_THROWS_AS(slots[99u], std::runtime_error);
	REQUIRE_THROWS_AS(slots.generation(99u), std::runtime_error);
	REQUIRE_THROWS_AS(slots.handle_at(99u), std::runtime_error);

	REQUIRE_FALSE(slots.valid(99u));
	REQUIRE_FALSE(slots.valid(99u, 0u));
	REQUIRE_FALSE(slots.is_allocated_root(99u));
}

TEST_CASE("multi_slot_vector destroys each element exactly once", "[multi_slot_vector]")
{
	TrackedElement::ResetCounters();

	{
		core::multi_slot_vector<TrackedElement> slots(4);
		REQUIRE(TrackedElement::s_Live == 4);

		auto handle = slots.allocate_slots(3u);
		REQUIRE(TrackedElement::s_Live == 4);
		REQUIRE(slots[0u].value == 0);

		slots[0u] = TrackedElement(5);
		REQUIRE(slots[0u].value == 5);

		// erase resets each slot in place -- it must not destroy the element and
		// then assign over the corpse.
		slots.erase(handle);
		REQUIRE(TrackedElement::s_DoubleDestroy == 0);
		REQUIRE(TrackedElement::s_AssignToDead == 0);
		REQUIRE(TrackedElement::s_Live == 4);

		auto reused = slots.allocate_slots(3u);
		REQUIRE(reused.index == handle.index);
		REQUIRE(slots[0u].value == 0);
	}

	REQUIRE(test::TrackingClean());
}

TEST_CASE("multi_slot_vector clear and reset release every element", "[multi_slot_vector]")
{
	TrackedElement::ResetCounters();

	{
		core::multi_slot_vector<TrackedElement> slots(3);
		(void)slots.allocate_slots(2u);

		slots.reset(5u);
		REQUIRE(TrackedElement::s_Live == 5);
		REQUIRE(slots.size() == 5u);
		REQUIRE_FALSE(slots.is_allocated_root(0u));

		slots.clear();
		REQUIRE(TrackedElement::s_Live == 0);
		REQUIRE(slots.size() == 0u);
	}

	REQUIRE(test::TrackingClean());
}
