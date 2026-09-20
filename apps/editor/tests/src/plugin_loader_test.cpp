#include "Plugins/EditorRegistry.h"
#include "Plugins/plugin_loader.h"

#include <QLibrary>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cstdint>
#include <editor_api/PluginDescriptor.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

#if defined(EDITOR_PLUGIN_FIXTURE)
	struct Sandbox
	{
		fs::path root =
			fs::temp_directory_path() /
			("bernini_plugin_loader_" +
		     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		Sandbox()
		{
			std::error_code error;
			fs::remove_all(root, error);
			fs::create_directories(root);
		}

		~Sandbox()
		{
			std::error_code error;
			fs::remove_all(root, error);
		}
	};

	fs::path
	WriteDescriptor(
		const fs::path&                       root,
		const std::string&                    id,
		const fs::path&                       fixture,
		const editor::plugins::BuildIdentity& build,
		const std::vector<std::string>&       dependencies = {})
	{
		fs::create_directories(root);
		const fs::path module = fixture.filename();
		fs::copy_file(fixture, root / module, fs::copy_options::overwrite_existing);
		std::ofstream(root / editor::c_PluginDescriptorFileName)
			<< nlohmann::json{
				   { "version", editor::c_PluginDescriptorVersion },
				   { "id", id },
				   { "engineBuildId", build.id },
				   { "configuration", build.configuration },
				   { "runtime", module.generic_string() },
				   { "editor", module.generic_string() },
				   { "dependencies", dependencies },
			   }
				   .dump(2);
		return root;
	}

	void
	SetSdkStamp(const editor::plugins::BuildIdentity& build, fs::file_time_type time)
	{
		std::ofstream(build.sdkStamp) << "sdk";
		fs::last_write_time(build.sdkStamp, time);
	}

	uint32_t
	AssetFactoryCalls(const fs::path& module)
	{
		QLibrary library(QString::fromStdWString(module.wstring()));
		REQUIRE(library.load());
		const auto calls = reinterpret_cast<uint32_t (*)()>(
			library.resolve("BerniniEditorSdkTestAssetFactoryCalls"));
		REQUIRE(calls != nullptr);
		return calls();
	}
#endif
}

#if defined(EDITOR_PLUGIN_FIXTURE)
TEST_CASE("A compatible local plugin loads both module halves", "[plugins][loader]")
{
	Sandbox sandbox;
	auto    build  = editor::plugins::CurrentBuildIdentity();
	build.sdkStamp = sandbox.root / "sdk.stamp";
	SetSdkStamp(build, fs::file_time_type::clock::now() - std::chrono::hours(1));
	const fs::path plugin =
		WriteDescriptor(sandbox.root / "valid", "sample.valid", EDITOR_PLUGIN_FIXTURE, build);

	const editor::plugins::PluginSession session = editor::plugins::PluginSession::Load(
		std::vector<std::string>{ "sample.valid" },
		std::vector<fs::path>{ plugin },
		build,
		sandbox.root / "plugin-copies",
		editor::plugins::PluginBinaryCopyMode::kAlways);

	CHECK(session.Ids() == std::vector<std::string>{ "sample.valid" });
	CHECK(session.KindRegistry()->FindById("sample.fixture") != nullptr);
	CHECK(session.EditorPlugins().size() == 1);
	CHECK(session.Contributions().FindPanel("sample.fixture_panel") != nullptr);
	CHECK(
		fs::is_regular_file(
			sandbox.root / "plugin-copies" / "sample.valid" /
			fs::path(EDITOR_PLUGIN_FIXTURE).filename()));
}

TEST_CASE("Compatibility failures do not invoke a plugin entry point", "[plugins][loader]")
{
	Sandbox sandbox;
	auto    build  = editor::plugins::CurrentBuildIdentity();
	build.sdkStamp = sandbox.root / "sdk.stamp";
	SetSdkStamp(build, fs::file_time_type::clock::now() - std::chrono::hours(1));

	const fs::path plugin = WriteDescriptor(
		sandbox.root / "rejected",
		"sample.rejected",
		EDITOR_PLUGIN_REJECTED_FIXTURE,
		build);
	const fs::path module = plugin / fs::path(EDITOR_PLUGIN_REJECTED_FIXTURE).filename();

	SECTION("a different engine build")
	{
		auto json =
			nlohmann::json::parse(std::ifstream(plugin / editor::c_PluginDescriptorFileName));
		json["engineBuildId"] = "another-build";
		std::ofstream(plugin / editor::c_PluginDescriptorFileName) << json.dump(2);
		CHECK_THROWS_WITH(
			editor::plugins::PluginSession::Load(
				std::vector<std::string>{ "sample.rejected" },
				std::vector<fs::path>{ plugin },
				build,
				sandbox.root / "plugin-copies"),
			Catch::Matchers::ContainsSubstring("another-build"));
	}

	SECTION("an older module")
	{
		SetSdkStamp(build, fs::file_time_type::clock::now() + std::chrono::hours(1));
		CHECK_THROWS_WITH(
			editor::plugins::PluginSession::Load(
				std::vector<std::string>{ "sample.rejected" },
				std::vector<fs::path>{ plugin },
				build,
				sandbox.root / "plugin-copies"),
			Catch::Matchers::ContainsSubstring("older"));
	}

	SECTION("a missing declared dependency")
	{
		auto json =
			nlohmann::json::parse(std::ifstream(plugin / editor::c_PluginDescriptorFileName));
		json["dependencies"] = { "missing-library" };
		std::ofstream(plugin / editor::c_PluginDescriptorFileName) << json.dump(2);
		CHECK_THROWS_WITH(
			editor::plugins::PluginSession::Load(
				std::vector<std::string>{ "sample.rejected" },
				std::vector<fs::path>{ plugin },
				build,
				sandbox.root / "plugin-copies"),
			Catch::Matchers::ContainsSubstring("missing"));
	}

	CHECK(AssetFactoryCalls(module) == 0);
}

TEST_CASE("A colliding module rejects the plugin session", "[plugins][loader]")
{
	Sandbox sandbox;
	auto    build  = editor::plugins::CurrentBuildIdentity();
	build.sdkStamp = sandbox.root / "sdk.stamp";
	SetSdkStamp(build, fs::file_time_type::clock::now() - std::chrono::hours(1));
	const fs::path first =
		WriteDescriptor(sandbox.root / "first", "sample.first", EDITOR_PLUGIN_FIXTURE, build);
	const fs::path second = WriteDescriptor(
		sandbox.root / "second",
		"sample.second",
		EDITOR_PLUGIN_COLLISION_FIXTURE,
		build);

	CHECK_THROWS_WITH(
		editor::plugins::PluginSession::Load(
			std::vector<std::string>{ "sample.first", "sample.second" },
			std::vector<fs::path>{ first, second },
			build,
			sandbox.root / "plugin-copies",
			editor::plugins::PluginBinaryCopyMode::kNever),
		Catch::Matchers::ContainsSubstring("collides"));
}

TEST_CASE("Editor contribution collisions reject the private session", "[plugins][loader]")
{
	Sandbox sandbox;
	auto    build  = editor::plugins::CurrentBuildIdentity();
	build.sdkStamp = sandbox.root / "sdk.stamp";
	SetSdkStamp(build, fs::file_time_type::clock::now() - std::chrono::hours(1));
	const fs::path first =
		WriteDescriptor(sandbox.root / "first", "sample.first", EDITOR_PLUGIN_FIXTURE, build);
	const fs::path second = WriteDescriptor(
		sandbox.root / "second",
		"sample.second",
		EDITOR_PLUGIN_COLLISION_FIXTURE,
		build);
	for (const fs::path& directory : { first, second })
	{
		auto json =
			nlohmann::json::parse(std::ifstream(directory / editor::c_PluginDescriptorFileName));
		json["runtime"] = "";
		std::ofstream(directory / editor::c_PluginDescriptorFileName) << json.dump(2);
	}

	CHECK_THROWS_WITH(
		editor::plugins::PluginSession::Load(
			std::vector<std::string>{ "sample.first", "sample.second" },
			std::vector<fs::path>{ first, second },
			build,
			sandbox.root / "plugin-copies",
			editor::plugins::PluginBinaryCopyMode::kNever),
		Catch::Matchers::ContainsSubstring("collides"));
}
#endif

TEST_CASE("Changing the project plugin list requires a restart", "[plugins][loader]")
{
	CHECK_FALSE(
		editor::plugins::OpeningNeedsPluginRelaunch(
			std::vector<std::string>{ "studio.ai" },
			std::vector<std::string>{ "studio.ai" }));
	CHECK(
		editor::plugins::OpeningNeedsPluginRelaunch(
			std::vector<std::string>{ "studio.ai" },
			std::vector<std::string>{ "studio.quest" }));
}
