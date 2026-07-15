#include "idl/Entry.h"
#include "idl/Range.h"
#include "idl/RangeWithCount.h"

TEST_CASE("A default-constructed offset primitive is null", "[idl]")
{
	// The Slang source of truth defaults these to 0xFFFFFFFF; the C++ mirrors must agree, or a
	// zero-initialized struct silently points at element 0 instead of reading as null.
	CHECK(bgl::idl::Entry().Null());
	CHECK(bgl::idl::Range().Null());
	CHECK(bgl::idl::RangeWithCount().Null());

	// Offset 0 is a valid element index, not the null sentinel.
	CHECK_FALSE(bgl::idl::Entry{ 0 }.Null());
}
