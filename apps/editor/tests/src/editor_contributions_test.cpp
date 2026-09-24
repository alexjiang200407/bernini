#include "Plugins/EditorHost.h"
#include "Plugins/EditorRegistry.h"
#include "util/editor_language.h"
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorImporter.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IThumbnailProvider.h>

#include <QWidget>
#include <assetlib/AssetStore.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/Thumbnail.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <filesystem>
#include <memory>
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

	class NullFactory final : public editor::IEditorPanelFactory
	{
	public:
		editor::EditorPanel*
		Create(editor::IEditorHost&, QWidget*) override
		{
			return nullptr;
		}
	};
	class Importer final : public editor::IEditorImporter
	{
	public:
		void
		Import(editor::IEditorHost&, const std::filesystem::path&, std::string_view) override
		{}
	};
	class ThumbnailProvider final : public editor::IThumbnailProvider
	{
	public:
		editor::Thumbnail
		Describe(const assetlib::AssetStore&, std::string_view) const override
		{
			return {};
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
	registry.Register(
		*plugin,
		editor::ReadLocalizationDirectory(EDITOR_PLUGIN_SAMPLE_LOCALIZATION));

	REQUIRE(registry.Catalogs().size() == 1);
	REQUIRE(registry.Menus().size() == 1);
	REQUIRE(registry.Panels().size() == 1);
	REQUIRE(registry.AssetEditors().size() == 1);
	REQUIRE(registry.Actions().size() == 2);
	CHECK(registry.FindPanel("sample.overview") != nullptr);
	CHECK(registry.FindAssetEditor(".bexample") != nullptr);
	CHECK(registry.FindAssetEditor(".unknown") == nullptr);

	editor::LanguageResolver language;
	for (const editor::TranslationCatalog& catalog : registry.Catalogs())
		language.RegisterCatalog(catalog);
	TemporaryStore              store;
	std::string                 shown;
	editor::plugins::EditorHost host(
		store.store,
		language,
		nullptr,
		nullptr,
		true,
		{ .showPanel = [&](const std::string_view id) { shown = id; } });
	registry.Actions().front().action->Invoke(host, {});
	CHECK(shown == "sample.overview");
	CHECK(
		host.GetLanguageResolver().Resolve({ "sample.editor", "overview", "Project tools" }) ==
		"Project tools");
}

TEST_CASE("A host with no import refuses rather than answering empty", "[plugins][registry]")
{
	TemporaryStore              store;
	editor::LanguageResolver    language;
	editor::plugins::EditorHost host(store.store, language, nullptr, nullptr, true, {});

	// An empty key means an import produced no mesh, which a caller drops in silence. A host that
	// cannot import at all must not arrive as that same answer.
	CHECK_THROWS(host.ImportMeshSource("/downloads/crate.glb"));
}

TEST_CASE(
	"Contribution registration rejects collisions without changing the registry",
	"[plugins][registry]")
{
	editor::plugins::EditorRegistry registry;
	registry.AddPanel(
		editor::PanelDesc()
			.SetId("sample.panel")
			.SetTitle(Text("panel"))
			.AddFactory<NullFactory>());

	CHECK_THROWS_WITH(
		registry.AddPanel(
			editor::PanelDesc()
				.SetId("sample.panel")
				.SetTitle(Text("second"))
				.AddFactory<NullFactory>()),
		Catch::Matchers::ContainsSubstring("collides"));
	CHECK(registry.Panels().size() == 1);
}

TEST_CASE(
	"Import and thumbnail extensions have one production dispatch target",
	"[plugins][registry]")
{
	editor::plugins::EditorRegistry registry;
	registry.AddImporter(
		editor::ImporterDesc()
			.SetId("sample.import")
			.AddExtension(".source")
			.AddImporter<Importer>());
	registry.AddImporter(
		editor::ImporterDesc()
			.SetId("sample.ai_import")
			.AddExtension(".ai_state")
			.AddImporter<Importer>());
	registry.AddThumbnailProvider(
		editor::ThumbnailProviderDesc()
			.SetId("sample.thumbnail")
			.AddExtension(".bexample")
			.AddProvider<ThumbnailProvider>());

	CHECK(registry.FindImporter(".source") == &registry.Importers().front());
	CHECK(registry.FindImporter(".ai_state") != nullptr);
	CHECK(registry.FindThumbnailProvider(".bexample") == &registry.ThumbnailProviders().front());
	CHECK_THROWS_WITH(
		registry.AddImporter(
			editor::ImporterDesc()
				.SetId("sample.second_import")
				.AddExtension(".source")
				.AddImporter<Importer>()),
		Catch::Matchers::ContainsSubstring("collides"));
}

TEST_CASE(
	"Failed plugin registration destroys its owned batch before the plugin",
	"[plugins][registry][lifetime]")
{
	struct State
	{
		int  destroyed     = 0;
		bool pluginAlive   = true;
		bool ownerOutlived = true;
	};
	class Factory final : public editor::IEditorPanelFactory
	{
	public:
		explicit Factory(std::shared_ptr<State> state) : m_State(std::move(state)) {}
		~Factory() override
		{
			++m_State->destroyed;
			m_State->ownerOutlived &= m_State->pluginAlive;
		}
		editor::EditorPanel*
		Create(editor::IEditorHost&, QWidget*) override
		{
			return nullptr;
		}

	private:
		std::shared_ptr<State> m_State;
	};
	class FailingPlugin final : public editor::IEditorPlugin
	{
	public:
		explicit FailingPlugin(std::shared_ptr<State> state) : m_State(std::move(state)) {}
		~FailingPlugin() override { m_State->pluginAlive = false; }
		void
		Register(editor::IEditorRegistry& registry) override
		{
			registry.AddTranslations({ "failed.editor", { { "title", "en", "Title" } } });
			registry.AddMenu(
				editor::MenuDesc()
					.SetId("failed.menu")
					.SetParentId("sample.menu")
					.SetTitle(Text("menu")));
			registry.AddPanel(
				editor::PanelDesc()
					.SetId("failed.panel")
					.SetTitle(Text("panel"))
					.AddFactory<Factory>(m_State));
			registry.AddImporter(
				editor::ImporterDesc()
					.SetId("failed.importer")
					.AddExtension(".failed")
					.AddImporter<Importer>());
			registry.AddThumbnailProvider(
				editor::ThumbnailProviderDesc()
					.SetId("failed.thumbnail")
					.AddExtension(".failed")
					.AddProvider<ThumbnailProvider>());
			m_Sample->Register(registry);
			registry.AddPanel(
				editor::PanelDesc()
					.SetId("sample.panel")
					.SetTitle(Text("duplicate"))
					.AddFactory<Factory>(m_State));
		}

	private:
		std::shared_ptr<State>  m_State;
		editor::EditorPluginPtr m_Sample = sample::CreateEditorPlugin();
	};
	auto                            validPlugin = sample::CreateEditorPlugin();
	editor::plugins::EditorRegistry registry;
	registry.AddMenu(
		editor::MenuDesc()
			.SetId("sample.menu")
			.SetParentId(std::string(editor::c_ToolsMenuId))
			.SetTitle(Text("menu")));
	registry.AddPanel(
		editor::PanelDesc()
			.SetId("sample.panel")
			.SetTitle(Text("panel"))
			.AddFactory<NullFactory>());
	const auto* original = registry.Panels().front().factory.get();
	auto        state    = std::make_shared<State>();
	{
		FailingPlugin plugin(state);
		CHECK_THROWS_WITH(
			registry.Register(plugin),
			Catch::Matchers::ContainsSubstring("collides"));
		CHECK(state->destroyed == 2);
		CHECK(state->ownerOutlived);
		CHECK(registry.Catalogs().empty());
		REQUIRE(registry.Menus().size() == 1);
		REQUIRE(registry.Panels().size() == 1);
		CHECK(registry.Panels().front().factory.get() == original);
		CHECK(registry.Actions().empty());
		CHECK(registry.AssetEditors().empty());
		CHECK(registry.Importers().empty());
		CHECK(registry.ThumbnailProviders().empty());
	}
	CHECK_FALSE(state->pluginAlive);
	CHECK(state->ownerOutlived);
	registry.Register(*validPlugin);
	CHECK(registry.Actions().size() == 2);
}

TEST_CASE("Null contribution objects are rejected", "[plugins][registry]")
{
	editor::plugins::EditorRegistry registry;
	CHECK_THROWS(
		registry.AddPanel(editor::PanelDesc().SetId("sample.panel").SetTitle(Text("panel"))));
	CHECK_THROWS(registry.AddAssetEditor(
		editor::AssetEditorDesc()
			.SetId("sample.editor")
			.SetTitle(Text("editor"))
			.AddExtension(".bexample")));
	CHECK_THROWS(registry.AddAction(
		editor::ActionDesc()
			.SetId("sample.action")
			.SetTitle(Text("action"))
			.SetMenuId(std::string(editor::c_ToolsMenuId))));
	CHECK_THROWS(registry.AddImporter(
		editor::ImporterDesc().SetId("sample.importer").AddExtension(".source")));
	CHECK_THROWS(registry.AddThumbnailProvider(
		editor::ThumbnailProviderDesc().SetId("sample.thumbnail").AddExtension(".bexample")));
	CHECK(registry.Panels().empty());
	CHECK(registry.AssetEditors().empty());
	CHECK(registry.Actions().empty());
	CHECK(registry.Importers().empty());
	CHECK(registry.ThumbnailProviders().empty());
}
