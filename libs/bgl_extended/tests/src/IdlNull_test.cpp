#include <bgl_common/idl/Entry.h>
#include <bgl_common/idl/Range.h>
#include <bgl_common/idl/RangeWithCount.h>

TEST_CASE("A default-constructed offset primitive is null", "[idl]")
{
	// The Slang source of truth defaults these to 0, the element every EntryBuffer and RangeBuffer
	// reserves; the C++ mirrors must agree, or a zero-initialized struct points at a live element
	// instead of reading as null.
	CHECK(bgl::idl::Entry().Null());
	CHECK(bgl::idl::Range().Null());
	CHECK(bgl::idl::RangeWithCount().Null());

	// Any offset a buffer hands out is above the reserved one, so it is not null.
	CHECK_FALSE(bgl::idl::Entry{ 1 }.Null());
}

TEST_CASE("Assigning a null handle leaves an offset primitive null", "[idl]")
{
	// slot_handle carries its own invalid_index, which is not this null: assigning one straight
	// through would write an offset that reads as live and dereferences out of bounds.
	auto entry = bgl::idl::Entry();
	entry      = core::slot_handle();
	CHECK(entry.Null());

	auto range = bgl::idl::Range();
	range      = core::multi_slot_handle();
	CHECK(range.Null());

	auto rangeWithCount = bgl::idl::RangeWithCount();
	rangeWithCount      = core::multi_slot_handle();
	CHECK(rangeWithCount.Null());
	CHECK(rangeWithCount.count == 0);
}
