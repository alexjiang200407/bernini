#include "Plugins/EditorHost.h"
#include "Plugins/EditorRegistry.h"
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>

#include <QWidget>
#include <assetlib/AssetStore.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/IEditorRegistry.h>
#include <editor_api/LocalizedText.h>
#include <filesystem>
#include <sample.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
	struct TemporaryStore
	{
		std::filesystem::path root =
			std::filesystem::temp_directory_path() / "bernini_editor_contributions";
		assetlib::AssetStore store;

		TemporaryStore() : store(CreateRoot()) {}

		~TemporaryStore()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}

		std::filesystem::path
		CreateRoot()
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
			std::filesystem::create_directories(root);
			return root;
		}
	};

	editor::LocalizedText
	Text(std::string key)
	{
		return { "sample.editor", std::move(key), "fallback" };
	}
}

TEST_CASE("The production registry owns and dispatches sample contributions", "[plugins][registry]")
{
	auto                            plugin = sample::CreateEditorPlugin();
	editor::plugins::EditorRegistry registry;
	plugin->Register(registry);

	REQUIRE(registry.Catalogs().size() == 1);
	REQUIRE(registry.Menus().size() == 1);
	REQUIRE(registry.Panels().size() == 1);
	REQUIRE(registry.AssetEditors().size() == 1);
	REQUIRE(registry.Actions().size() == 1);
	CHECK(registry.FindPanel("sample.overview") != nullptr);
	CHECK(registry.FindAssetEditor(".bexample") != nullptr);
	CHECK(registry.FindAssetEditor(".unknown") == nullptr);

	TemporaryStore              store;
	std::string                 shown;
	editor::plugins::EditorHost host(
		store.store,
		registry.Catalogs(),
		nullptr,
		nullptr,
		true,
		{ .showPanel = [&](const std::string_view id) { shown = id; } });
	registry.Actions().front().invoke(host, {});
	CHECK(shown == "sample.overview");
	CHECK(
		host.GetLanguageResolver().Resolve({ "sample.editor", "overview", "Project tools" }) ==
		"Project tools");
}

TEST_CASE(
	"Contribution registration rejects collisions without changing the registry",
	"[plugins][registry]")
{
	editor::plugins::EditorRegistry registry;
	const auto                      create = [](editor::IEditorHost&, QWidget*) {
		return static_cast<editor::EditorPanel*>(nullptr);
	};
	registry.AddPanel({ "sample.panel", Text("panel"), create });

	CHECK_THROWS_WITH(
		registry.AddPanel({ "sample.panel", Text("second"), create }),
		Catch::Matchers::ContainsSubstring("collides"));
	CHECK(registry.Panels().size() == 1);
}

TEST_CASE(
	"Import and thumbnail extensions have one production dispatch target",
	"[plugins][registry]")
{
	editor::plugins::EditorRegistry registry;
	registry.AddImporter(
		{ "sample.import",
	      { ".source" },
	      [](editor::IEditorHost&, const std::filesystem::path&, std::string_view) {} });
	registry.AddImporter(
		{ "sample.ai_import",
	      { ".ai_state" },
	      [](editor::IEditorHost&, const std::filesystem::path&, std::string_view) {} });
	registry.AddThumbnailProvider(
		{ "sample.thumbnail", { ".bexample" }, [](const assetlib::AssetStore&, std::string_view) {
			 return editor::Thumbnail{};
		 } });

	CHECK(registry.FindImporter(".source") == &registry.Importers().front());
	CHECK(registry.FindImporter(".ai_state") != nullptr);
	CHECK(registry.FindThumbnailProvider(".bexample") == &registry.ThumbnailProviders().front());
	CHECK_THROWS_WITH(
		registry.AddImporter(
			{ "sample.second_import",
	          { ".source" },
	          [](editor::IEditorHost&, const std::filesystem::path&, std::string_view) {} }),
		Catch::Matchers::ContainsSubstring("collides"));
}
