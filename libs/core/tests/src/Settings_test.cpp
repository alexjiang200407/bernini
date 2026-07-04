#include <catch2/catch_approx.hpp>
#include <core/settings/Settings.h>

namespace
{
	// Writes `contents` to a temp file in the cwd and returns its path; the caller
	// removes it. Mirrors how the bgl resize tests stage throwaway files.
	std::filesystem::path
	WriteTempJson(const std::string& name, const std::string& contents)
	{
		const auto path = std::filesystem::path(name);
		std::ofstream(path) << contents;
		return path;
	}

	constexpr const char* kConfig = R"({
		"graphics": {
			"msaa": 4,
			"gamma": 2.2,
			"vsync": true,
			"adapter": "RTX 4090",
			"exposure": -7
		},
		"audio": {
			"volume": 80
		}
	})";
}

TEST_CASE("Settings reads scalars with implicit conversions", "[settings]")
{
	const auto     path = WriteTempJson("settings_scalars.json", kConfig);
	core::Settings settings(path);

	const int          msaa     = settings["graphics"]["msaa"];
	const unsigned int msaaU    = settings["graphics"]["msaa"];
	const float        msaaF    = settings["graphics"]["msaa"];
	const float        gamma    = settings["graphics"]["gamma"];
	const bool         vsync    = settings["graphics"]["vsync"];
	const std::string  adapter  = settings["graphics"]["adapter"];
	const int          exposure = settings["graphics"]["exposure"];
	const int64_t      volume   = settings["audio"]["volume"];

	CHECK(msaa == 4);
	CHECK(msaaU == 4u);
	CHECK(msaaF == Catch::Approx(4.0f));
	CHECK(gamma == Catch::Approx(2.2f));
	CHECK(vsync == true);
	CHECK(adapter == "RTX 4090");
	CHECK(exposure == -7);
	CHECK(volume == 80);

	std::filesystem::remove(path);
}

TEST_CASE("Settings returns defaults for missing keys", "[settings]")
{
	const auto     path = WriteTempJson("settings_missing.json", kConfig);
	core::Settings settings(path);

	// A missing key yields a null accessor, not a throw.
	CHECK(settings["graphics"]["nope"].IsNull());
	CHECK(settings["does_not_exist"].IsNull());

	// Implicit conversion of a null accessor gives the type's default.
	const int   missingInt  = settings["graphics"]["nope"];
	const bool  missingBool = settings["graphics"]["nope"];
	const float missingF    = settings["graphics"]["nope"];
	CHECK(missingInt == 0);
	CHECK(missingBool == false);
	CHECK(missingF == Catch::Approx(0.0f));

	// GetOrDefault supplies an explicit fallback.
	CHECK(settings["graphics"]["nope"].GetOrDefault(1) == 1);
	CHECK(settings["graphics"]["nope"].GetOrDefault(2.5f) == Catch::Approx(2.5f));
	CHECK(settings["graphics"]["nope"].GetOrDefault(true) == true);
	CHECK(settings["graphics"]["nope"].GetOrDefault(std::string("none")) == "none");

	// A present value overrides the default.
	CHECK(settings["graphics"]["msaa"].GetOrDefault(1) == 4);

	std::filesystem::remove(path);
}

TEST_CASE("Settings chains through null without throwing", "[settings]")
{
	const auto     path = WriteTempJson("settings_chain.json", kConfig);
	core::Settings settings(path);

	// Indexing past a missing branch stays null all the way down.
	auto deep = settings["nope"]["deeper"]["deepest"];
	CHECK(deep.IsNull());
	CHECK(deep.GetOrDefault(99) == 99);

	// Indexing into a scalar (not an object) is also null, not an error.
	CHECK(settings["graphics"]["msaa"]["x"].IsNull());

	std::filesystem::remove(path);
}

TEST_CASE("Settings returns defaults on type mismatch", "[settings]")
{
	const auto     path = WriteTempJson("settings_mismatch.json", kConfig);
	core::Settings settings(path);

	// A string requested as a number => default, not a throw.
	CHECK(static_cast<int>(settings["graphics"]["adapter"]) == 0);
	CHECK(settings["graphics"]["adapter"].GetOrDefault(3) == 3);

	// A number requested as a string => default.
	CHECK(static_cast<std::string>(settings["graphics"]["msaa"]).empty());
	CHECK(settings["graphics"]["msaa"].GetOrDefault(std::string("x")) == "x");

	std::filesystem::remove(path);
}

TEST_CASE("Settings throws on bad input", "[settings]")
{
	CHECK_THROWS_AS(core::Settings("this_file_does_not_exist.json"), std::runtime_error);

	const auto path = WriteTempJson("settings_invalid.json", "{ not valid json ");
	CHECK_THROWS_AS(core::Settings(path), std::runtime_error);
	std::filesystem::remove(path);
}
