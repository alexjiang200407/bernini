#include <assetlib/assetlib.h>

TEST_CASE("assetlib exposes a version string", "[assetlib]")
{
	REQUIRE(assetlib::version() != nullptr);
}
