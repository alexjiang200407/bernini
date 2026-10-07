#include <catch2/catch_test_macros.hpp>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <core/ref/WeakRef.h>

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

TEST_CASE(
	"A WeakRef locks to a reference while one is held, and to null when it holds nothing",
	"[ref]")
{
	auto* counted = new Counted();
	{
		const auto weak   = core::WeakRef<Counted>(counted);
		const auto locked = weak.Lock();
		CHECK(locked.Get() == counted);
		CHECK(counted->GetRefCount() == 2);
	}
	CHECK(counted->GetRefCount() == 1);
	CHECK(counted->Release() == 0);

	CHECK(core::WeakRef<Counted>().Lock() == nullptr);
}
