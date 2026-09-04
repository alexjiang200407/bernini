#include <assetlib/assetlib.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("assetlib exposes a version string", "[assetlib]")
{
	REQUIRE(assetlib::version() != nullptr);
}
