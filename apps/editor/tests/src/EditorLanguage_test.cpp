#include "Plugins/EditorRegistry.h"
#include "util/editor_language.h"

#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <default_editor/plugin.h>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
	struct TemporaryDirectory
	{
		std::filesystem::path root =
			std::filesystem::temp_directory_path() / "bernini_editor_language";

		TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		void
		Write(const std::string& name, const std::string_view contents) const
		{
			std::ofstream(root / name, std::ios::binary) << contents;
		}
	};

	class TranslatingPlugin final : public editor::IEditorPlugin
	{
	public:
		void
		Register(editor::IEditorRegistry& registry) override
		{
			registry.AddTranslations({ "fixture.editor", { { "title", "en", "Fixture" } } });
		}
	};

	void
	RegisterAll(editor::LanguageResolver& language, const editor::plugins::EditorRegistry& registry)
	{
		for (const editor::TranslationCatalog& catalog : registry.Catalogs())
			language.RegisterCatalog(catalog);
	}
}

TEST_CASE("A localization directory is one catalog per CSV, named by its stem", "[localization]")
{
	TemporaryDirectory directory;
	directory.Write("fixture.b.csv", "key,en\ntitle,B\n");
	directory.Write("fixture.a.csv", "key,en\ntitle,A\n");
	directory.Write("notes.txt", "not a catalog");

	const auto catalogs = editor::ReadLocalizationDirectory(directory.root);
	REQUIRE(catalogs.size() == 2);
	CHECK(catalogs[0].context == "fixture.a");
	CHECK(catalogs[1].context == "fixture.b");
	CHECK(catalogs[0].entries.front().text == QString("A"));

	CHECK(editor::ReadLocalizationDirectory(directory.root / "absent").empty());
}

TEST_CASE("An invalid catalog is refused naming its file", "[localization]")
{
	TemporaryDirectory directory;
	directory.Write("fixture.editor.csv", "key,en\ntitle,A\ntitle,B\n");

	CHECK_THROWS_WITH(
		editor::ReadLocalizationDirectory(directory.root),
		Catch::Matchers::ContainsSubstring("fixture.editor.csv"));
}

TEST_CASE("The locale is read from the config and defaults to en", "[localization]")
{
	TemporaryDirectory directory;
	CHECK(editor::ConfiguredLocale(directory.root / "absent.json") == "en");

	directory.Write("config.json", R"({ "instanceName": "test" })");
	CHECK(editor::ConfiguredLocale(directory.root / "config.json") == "en");

	directory.Write("config.json", R"({ "locale": "fr" })");
	CHECK(editor::ConfiguredLocale(directory.root / "config.json") == "fr");
}

TEST_CASE("A discovered catalog registers with its plugin and rolls back with it", "[localization]")
{
	TranslatingPlugin               plugin;
	editor::plugins::EditorRegistry registry;

	SECTION("a context the plugin also registers in code")
	{
		CHECK_THROWS(
			registry.Register(plugin, { { "fixture.editor", { { "other", "en", "Other" } } } }));
	}
	SECTION("a context reserved for the host")
	{
		CHECK_THROWS(
			registry.Register(plugin, { { "editor.main", { { "title", "en", "Hijacked" } } } }));
	}

	CHECK(registry.Catalogs().empty());
}

TEST_CASE(
	"A test locale in a plugin's catalog changes what its title resolves to",
	"[localization]")
{
	TemporaryDirectory directory;
	directory.Write("bernini.material.csv", "key,en,xx\ntitle,Material Editor,Materiau\n");

	auto                            plugin = editor::defaults::CreatePlugin({});
	editor::plugins::EditorRegistry registry;
	registry.Register(*plugin, editor::ReadLocalizationDirectory(directory.root));

	editor::LanguageResolver language;
	RegisterAll(language, registry);
	const editor::LocalizedText title = registry.FindPanel("bernini.material")->title;
	CHECK(title.Resolve(language) == QString("Material Editor"));
	language.SetLocale("xx");
	CHECK(title.Resolve(language) == QString("Materiau"));
	language.SetLocale("yy");
	CHECK(title.Resolve(language) == QString("Material Editor"));
}

TEST_CASE("Every staged catalog registers", "[localization]")
{
	const auto host    = editor::ReadLocalizationDirectory(editor::DefaultLocalizationDirectory());
	const auto builtIn = editor::ReadLocalizationDirectory(editor::BuiltInLocalizationDirectory());
	REQUIRE_FALSE(host.empty());
	REQUIRE_FALSE(builtIn.empty());

	auto                            plugin = editor::defaults::CreatePlugin({});
	editor::plugins::EditorRegistry registry;
	registry.Register(*plugin, builtIn);

	editor::LanguageResolver language;
	for (const editor::TranslationCatalog& catalog : host) language.RegisterCatalog(catalog);
	RegisterAll(language, registry);
	for (const auto& panel : registry.Panels())
		CHECK(panel.title.Resolve(language) == panel.title.fallback);
}
