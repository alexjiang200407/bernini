#include <catch2/catch_test_macros.hpp>
#include <core/platform/util.h>
#include <format>
#include <optional>
#include <string>

TEST_CASE("An environment variable set here is the one read back", "[platform]")
{
	// Named for this process, so a shard of the suite running beside this one cannot be reading it.
	const std::string name = std::format("BERNINI_CORE_TESTS_ENV_{}", core::process_id());
	REQUIRE_FALSE(core::env_var(name.c_str()).has_value());

	REQUIRE(core::set_env_var(name.c_str(), "first"));
	CHECK(core::env_var(name.c_str()) == std::optional<std::string>("first"));

	REQUIRE(core::set_env_var(name.c_str(), "second"));
	CHECK(core::env_var(name.c_str()) == std::optional<std::string>("second"));
}
