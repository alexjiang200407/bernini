#include "TrackedElement.h"
#include <core/containers/slot_vector.h>

using test::TrackedElement;

TEST_CASE("slot_vector hands out ascending indices from a fresh reset", "[slot_vector]")
{
	core::slot_vector<int> slots(4);
	REQUIRE(slots.size() == 4u);

	REQUIRE(slots.allocate_slot().index == 0u);
	REQUIRE(slots.allocate_slot().index == 1u);
	REQUIRE(slots.allocate_slot().index == 2u);
	REQUIRE(slots.allocate_slot().index == 3u);
}

TEST_CASE("slot_vector reuses released slots and refuses to overrun maxSlots", "[slot_vector]")
{
	core::slot_vector<int> slots(2);

	auto a = slots.allocate_slot();
	auto b = slots.allocate_slot();
	REQUIRE_THROWS_AS(slots.allocate_slot(), std::runtime_error);

	slots.release_slot(a);
	auto c = slots.allocate_slot();
	REQUIRE(c.index == a.index);
	REQUIRE(slots.allocated(b.index));
}

TEST_CASE("slot_vector try_allocate returns a null handle on exhaustion", "[slot_vector]")
{
	core::slot_vector<int> slots(2);

	REQUIRE_FALSE(slots.try_allocate_slot().is_null());
	auto b = slots.try_allocate_and_emplace(5);
	REQUIRE(slots[b] == 5);

	// The pool is full: try_ hands back null where allocate_slot would throw.
	REQUIRE(slots.try_allocate_slot().is_null());

	// A freed slot is allocatable again.
	slots.release_slot(b);
	REQUIRE_FALSE(slots.try_allocate_slot().is_null());
}

TEST_CASE("slot_vector emplaces into a reused slot", "[slot_vector]")
{
	core::slot_vector<int> slots(2);

	auto a = slots.allocate_and_emplace(7);
	REQUIRE(slots[a] == 7);

	slots.release_slot(a);
	auto b = slots.allocate_and_emplace(9);
	REQUIRE(b.index == a.index);
	REQUIRE(slots[b] == 9);
}

TEST_CASE("slot_vector invalidates handles to a released slot", "[slot_vector]")
{
	core::slot_vector<int> slots(2);

	auto stale = slots.allocate_and_emplace(1);
	REQUIRE(slots.valid(stale));

	slots.release_slot(stale);
	REQUIRE_FALSE(slots.valid(stale));
	REQUIRE_FALSE(slots.allocated(stale.index));

	// The slot comes back at a higher generation, so the old handle stays dead
	// even though it names a live index again.
	auto fresh = slots.allocate_and_emplace(2);
	REQUIRE(fresh.index == stale.index);
	REQUIRE(fresh.generation != stale.generation);
	REQUIRE(slots.valid(fresh));
	REQUIRE_FALSE(slots.valid(stale));

	REQUIRE_THROWS_AS(slots[stale], std::runtime_error);
	REQUIRE_THROWS_AS(slots.release_slot(stale), std::runtime_error);
}

TEST_CASE("slot_vector rejects out-of-bounds and unallocated access", "[slot_vector]")
{
	core::slot_vector<int> slots(2);

	REQUIRE_THROWS_AS(slots[0u], std::runtime_error);
	REQUIRE_THROWS_AS(slots.release_slot(0u), std::runtime_error);
	REQUIRE_THROWS_AS(slots[99u], std::runtime_error);
	REQUIRE_THROWS_AS(slots.release_slot(99u), std::runtime_error);
	REQUIRE_THROWS_AS(slots.generation(99u), std::runtime_error);

	REQUIRE_FALSE(slots.valid(99u, 0u));
	REQUIRE_FALSE(slots.allocated(99u));
}

TEST_CASE("slot_vector grows without a slot limit", "[slot_vector]")
{
	core::slot_vector<int> slots;
	REQUIRE(slots.size() == 0u);

	for (uint32_t i = 0; i < 8u; ++i)
	{
		REQUIRE(slots.allocate_and_emplace(static_cast<int>(i)).index == i);
	}
	REQUIRE(slots.size() == 8u);
	REQUIRE(slots[3u] == 3);
}

TEST_CASE("slot_vector destroys each element exactly once", "[slot_vector]")
{
	TrackedElement::ResetCounters();

	{
		core::slot_vector<TrackedElement> slots(4);
		REQUIRE(TrackedElement::s_Live == 4);

		auto a = slots.allocate_and_emplace(11);
		auto b = slots.allocate_and_emplace(22);
		REQUIRE(slots[a].value == 11);

		// release_slot resets the slot in place -- it must not destroy the element
		// and then assign over the corpse.
		slots.release_slot(a);
		REQUIRE(TrackedElement::s_DoubleDestroy == 0);
		REQUIRE(TrackedElement::s_AssignToDead == 0);
		REQUIRE(slots[b].value == 22);

		// A released slot holds a default-constructed element again.
		auto c = slots.allocate_slot();
		REQUIRE(c.index == a.index);
		REQUIRE(slots[c].value == 0);
	}

	REQUIRE(test::TrackingClean());
}

TEST_CASE("slot_vector clear and reset release every element", "[slot_vector]")
{
	TrackedElement::ResetCounters();

	{
		core::slot_vector<TrackedElement> slots(3);
		(void)slots.allocate_and_emplace(1);

		slots.reset(5u);
		REQUIRE(TrackedElement::s_Live == 5);
		REQUIRE(slots.size() == 5u);
		REQUIRE_FALSE(slots.allocated(0u));

		slots.clear();
		REQUIRE(TrackedElement::s_Live == 0);
		REQUIRE(slots.size() == 0u);
	}

	REQUIRE(test::TrackingClean());
}
