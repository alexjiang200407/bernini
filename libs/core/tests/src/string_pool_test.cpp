#include <catch2/catch_test_macros.hpp>
#include <core/str/string_pool.h>
#include <cstdint>
#include <utility>
#include <vector>

using core::string_pool;

TEST_CASE("string_pool reserves offset 0 for the empty string", "[string_pool]")
{
	string_pool pool;

	// Offset 0 is what a zero-initialised nameOffset holds, so it has to read as "unnamed" on a pool
	// nothing has been added to as well as on a full one.
	CHECK(pool.at(0).empty());

	CHECK(pool.add("") == 0);
	CHECK(pool.empty());  // an empty string costs no bytes, so neither does an unnamed asset

	const uint32_t hips = pool.add("hips");
	CHECK(hips != 0);
	CHECK(pool.at(hips) == "hips");
	CHECK(pool.at(0).empty());
}

TEST_CASE("string_pool round-trips several names", "[string_pool]")
{
	string_pool pool;

	const uint32_t a = pool.add("hips");
	const uint32_t b = pool.add("spine");
	const uint32_t c = pool.add("head");

	CHECK(pool.at(a) == "hips");
	CHECK(pool.at(b) == "spine");
	CHECK(pool.at(c) == "head");

	SECTION("the same name added twice gets its own offset, and both read back")
	{
		const uint32_t again = pool.add("hips");
		CHECK(again != a);
		CHECK(pool.at(again) == "hips");
		CHECK(pool.at(a) == "hips");
	}

	SECTION("the bytes survive a trip through the form a file stores them in")
	{
		const string_pool restored(pool.bytes());
		CHECK(restored == pool);
		CHECK(restored.at(b) == "spine");
	}
}

TEST_CASE("string_pool reads nothing past its own end", "[string_pool]")
{
	string_pool pool;
	const auto  head = pool.add("head");

	CHECK(pool.at(static_cast<uint32_t>(pool.bytes().size())).empty());
	CHECK(pool.at(9999).empty());

	SECTION("a pool whose last name lost its terminator stops at the end")
	{
		// These bytes come off disk, so the terminator is not a given -- a truncated pool must clamp
		// rather than run the scan past the buffer.
		std::vector<char> truncated = pool.bytes();
		truncated.pop_back();

		const string_pool damaged(std::move(truncated));
		CHECK(damaged.at(head) == "head");  // clamped at the buffer, not run past it
	}
}
