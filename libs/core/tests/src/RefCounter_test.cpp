#include <catch2/catch_test_macros.hpp>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>

namespace
{
	class Counted final : public core::RefCounter<core::Ref>
	{
	public:
		Counted()               = default;
		Counted(const Counted&) = delete;
		Counted(Counted&&)      = delete;
		Counted&
		operator=(const Counted&) = delete;
		Counted&
		operator=(Counted&&) = delete;
	};
}

// TryAddRef is AddRef for an object something still holds; what it refuses, a count that reached
// zero, is an object mid-destruction, which no single-threaded case can hold to look at.
TEST_CASE("TryAddRef adds a reference while one is held", "[ref]")
{
	auto* counted = new Counted();
	CHECK(counted->GetRefCount() == 1);
	CHECK(counted->TryAddRef());
	CHECK(counted->GetRefCount() == 2);
	CHECK(counted->Release() == 1);
	CHECK(counted->Release() == 0);
}
