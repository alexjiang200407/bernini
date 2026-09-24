#include "sample.h"

#include <QLabel>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IEditorAction.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <exception>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	class RecordingRegistry final :
		public editor::IEditorRegistry,
		public assetlib::IAssetKindRegistry
	{
	public:
		std::vector<editor::TranslationCatalog>    catalogs;
		std::vector<editor::MenuDesc>              menus;
		std::vector<editor::PanelDesc>             panels;
		std::vector<editor::AssetEditorDesc>       editors;
		std::vector<editor::ActionDesc>            actions;
		std::vector<editor::ImporterDesc>          importers;
		std::vector<editor::ThumbnailProviderDesc> thumbnails;
		std::vector<assetlib::AssetKindPtr>        kinds;

		void
		AddTranslations(editor::TranslationCatalog catalog) override
		{
			catalogs.push_back(std::move(catalog));
		}

		void
		AddMenu(editor::MenuDesc desc) override
		{
			menus.push_back(std::move(desc));
		}
		void
		AddPanel(editor::PanelDesc desc) override
		{
			panels.push_back(std::move(desc));
		}
		void
		AddAssetEditor(editor::AssetEditorDesc desc) override
		{
			editors.push_back(std::move(desc));
		}
		void
		AddAction(editor::ActionDesc desc) override
		{
			actions.push_back(std::move(desc));
		}
		void
		AddImporter(editor::ImporterDesc desc) override
		{
			importers.push_back(std::move(desc));
		}
		void
		AddThumbnailProvider(editor::ThumbnailProviderDesc desc) override
		{
			thumbnails.push_back(std::move(desc));
		}
		void
		Add(assetlib::AssetKindPtr kind) override
		{
			kinds.push_back(std::move(kind));
		}
	};

	class RecordingHost final : public editor::IEditorHost
	{
	public:
		std::vector<std::string> shown;
		std::vector<std::string> opened;
		editor::LanguageResolver language;

		// What ImportMeshSource was asked for, and what it answers -- empty standing for every way
		// an import produces no mesh.
		std::vector<std::filesystem::path> imported;
		std::string                        importAnswer;

		const editor::ILanguageResolver&
		GetLanguageResolver() const noexcept override
		{
			return language;
		}

		const assetlib::AssetStore&
		GetStore() const noexcept override
		{
			std::terminate();
		}
		void
		InvokeRender(const editor::RenderWork&) override
		{
			throw std::runtime_error("Unexpected rendering");
		}
		editor::IEditorViewport*
		CreateViewport(QWidget*, const editor::ViewportDesc&) override
		{
			throw std::runtime_error("Unexpected viewport");
		}
		void
		ShowPanel(std::string_view id) override
		{
			shown.emplace_back(id);
		}
		void
		OpenAsset(std::string_view key) override
		{
			opened.emplace_back(key);
		}
		std::string
		ImportMeshSource(const std::filesystem::path& source) override
		{
			imported.push_back(source);
			return importAnswer;
		}
		void
		AssetChanged(std::string_view) override
		{
			throw std::runtime_error("Unexpected write");
		}
	};

	std::span<const std::byte>
	Bytes(std::string_view text)
	{
		return std::as_bytes(std::span(text));
	}
}

TEST_CASE(
	"A general tool tab registers without an asset kind or an open project",
	"[plugin][panel]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);
	REQUIRE(registry.kinds.empty());
	REQUIRE(registry.panels.size() == 1);
	REQUIRE(registry.panels.front().id == "sample.overview");
	REQUIRE(registry.actions.size() == 2);
	REQUIRE(registry.actions.front().id == "sample.show-overview");
	REQUIRE(registry.actions.front().extensions.empty());
	REQUIRE(registry.actions.front().menuId == "sample.tools");
	REQUIRE(registry.menus.size() == 1);
	REQUIRE(registry.menus.front().parentId == editor::c_ToolsMenuId);

	RecordingHost host;
	QWidget       root;
	const auto&   desc  = registry.panels.front();
	auto*         panel = desc.factory->Create(host, &root);
	REQUIRE(panel->parentWidget() == &root);
	REQUIRE(panel->GetHeldAssets().empty());
	REQUIRE(panel->CanClose());
	REQUIRE(registry.actions.front().action->IsEnabled(host, {}));
	registry.actions.front().action->Invoke(host, {});
	REQUIRE(host.shown == std::vector<std::string>{ desc.id });
}

TEST_CASE(
	"Deferred factories create project-owned widgets that release before the host",
	"[plugin][lifetime]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);
	RecordingHost                      host;
	QPointer<editor::AssetEditorPanel> observed;
	{
		QWidget root;
		REQUIRE(registry.editors.size() == 1);
		const auto& desc = registry.editors.front();
		REQUIRE(desc.extensions == std::vector<std::string>{ ".bexample" });
		observed = desc.factory->Create(host, &root);
		REQUIRE(observed->parentWidget() == &root);
		REQUIRE(observed->GetHeldAssets().empty());
		observed->OpenAsset("Authored/Example/first.bexample");
		observed->SetActive(false);
		REQUIRE_FALSE(observed->isEnabled());
		observed->SetActive(true);
		REQUIRE(observed->isEnabled());
		REQUIRE(
			observed->GetHeldAssets() ==
			std::vector<std::string>{ "Authored/Example/first.bexample" });
		observed->OpenAsset("Authored/Example/second.bexample");
		REQUIRE(
			observed->GetHeldAssets() ==
			std::vector<std::string>{ "Authored/Example/second.bexample" });
		REQUIRE(
			observed->findChild<QLabel*>()->text() == QString("Authored/Example/second.bexample"));
	}
	REQUIRE(observed.isNull());
	registry.actions.front().action->Invoke(host, {});
	REQUIRE(host.shown.size() == 1);
	RecordingHost nextHost;
	QWidget       nextRoot;
	auto*         nextPanel = registry.editors.front().factory->Create(nextHost, &nextRoot);
	REQUIRE(nextPanel->GetHeldAssets().empty());
	registry.actions.front().action->Invoke(nextHost, {});
	REQUIRE(nextHost.shown == host.shown);
	REQUIRE(host.shown.size() == 1);
}

TEST_CASE("A panel can report a changed document without reopening it", "[plugin][panel]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);
	RecordingHost host;
	QWidget       root;
	auto*         panel = registry.editors.front().factory->Create(host, &root);
	panel->OpenAsset("Authored/first.bexample");
	panel->OnAssetChanged("Authored/other.bexample");
	CHECK(panel->findChild<QLabel*>("sample.changedAsset")->text().isEmpty());
	panel->SetActive(false);
	panel->OnAssetChanged("Authored/first.bexample");
	CHECK(panel->findChild<QLabel*>("sample.changedAsset")->text() == "Authored/first.bexample");
	CHECK(panel->GetHeldAssets() == std::vector<std::string>{ "Authored/first.bexample" });
	panel->OpenAsset("Authored/second.bexample");
	CHECK(panel->findChild<QLabel*>("sample.changedAsset")->text().isEmpty());
	auto* overview = registry.panels.front().factory->Create(host, &root);
	CHECK_NOTHROW(overview->OnAssetChanged("Authored/first.bexample"));
}

TEST_CASE(
	"A registered document kind owns its semantics beyond the registration call",
	"[plugin][references]")
{
	auto              plugin = sample::CreateAssetPlugin();
	RecordingRegistry registry;
	plugin->RegisterKinds(registry);
	REQUIRE(registry.kinds.size() == 1);
	const auto& kind = *registry.kinds.front();
	REQUIRE(kind.GetDesc().id == "sample.document");
	REQUIRE(kind.GetDesc().extension == ".bexample");
	REQUIRE(kind.GetDesc().includeInPack);
	const auto document = Bytes(
		R"({"references":["Authored/a.bexample","Authored/a.bexample"],"future":{"keep":42}})");
	const auto references = kind.ReadReferences(document);
	REQUIRE(references.size() == 2);
	REQUIRE(references[0].target == references[1].target);
	REQUIRE(references[0].field != references[1].field);

	const std::vector<assetlib::DocumentReference> replacements = { { "Authored/b.bexample",
		                                                              references[1].field } };
	const auto rewritten = kind.RewriteReferences(document, replacements);
	const auto result    = nlohmann::json::parse(rewritten.begin(), rewritten.end());
	REQUIRE(result.at("references").at(0) == "Authored/a.bexample");
	REQUIRE(result.at("references").at(1) == "Authored/b.bexample");
	REQUIRE(result.at("future").at("keep") == 42);
	const auto migrated = kind.Migrate(rewritten);
	REQUIRE(nlohmann::json::parse(migrated.begin(), migrated.end()) == result);
	REQUIRE(kind.Migrate(migrated) == migrated);
}

TEST_CASE("Unreadable referrers fail instead of appearing unreferenced", "[plugin][references]")
{
	auto              plugin = sample::CreateAssetPlugin();
	RecordingRegistry registry;
	plugin->RegisterKinds(registry);
	const auto& kind = *registry.kinds.front();
	for (const auto text : { "broken",
	                         "{}",
	                         R"({"references":null})",
	                         R"({"references":[7]})",
	                         R"({"references":[""]})" })
	{
		REQUIRE_THROWS(kind.ReadReferences(Bytes(text)));
		REQUIRE_THROWS(kind.Migrate(Bytes(text)));
		REQUIRE_THROWS(kind.RewriteReferences(Bytes(text), {}));
	}
	const auto valid = Bytes(R"({"references":["Authored/a.bexample"]})");
	const std::vector<assetlib::DocumentReference> replacements = { { "Authored/b.bexample",
		                                                              "missing" } };
	REQUIRE_THROWS(kind.RewriteReferences(valid, replacements));
	REQUIRE(kind.ReadReferences(valid).front().target == "Authored/a.bexample");
}

namespace
{
	/** The sample's importing action, which is not the one every other case reaches for. */
	const editor::ActionDesc&
	ImportAction(const RecordingRegistry& registry)
	{
		const auto found =
			std::ranges::find(registry.actions, "sample.import-source", &editor::ActionDesc::id);
		REQUIRE(found != registry.actions.end());
		return *found;
	}
}

TEST_CASE("An imported source is opened by the key the host answers with", "[plugin][import]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);

	RecordingHost host;
	host.importAnswer = "Derived/Meshes/crate.bmesh";

	const editor::ActionDesc& action    = ImportAction(registry);
	const auto                selection = std::vector<std::string>{ "/downloads/crate.glb" };
	REQUIRE(action.action->IsEnabled(host, selection));
	action.action->Invoke(host, selection);

	// The source reaches the host as given -- a path outside the project, which no key could name.
	REQUIRE(host.imported == std::vector<std::filesystem::path>{ "/downloads/crate.glb" });

	// And its answer is a key, so it is usable as one without the caller resolving anything.
	REQUIRE(host.opened == std::vector<std::string>{ "Derived/Meshes/crate.bmesh" });
}

TEST_CASE("An import that produced no mesh opens nothing", "[plugin][import]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);

	// Declined, cancelled, failed, or imported for its clips alone: the contract makes them one
	// answer, because the host has already reported whichever it was.
	RecordingHost host;
	host.importAnswer = std::string();

	const editor::ActionDesc& action    = ImportAction(registry);
	const auto                selection = std::vector<std::string>{ "/downloads/crate.glb" };
	action.action->Invoke(host, selection);

	REQUIRE(host.imported.size() == 1);
	REQUIRE(host.opened.empty());
}

TEST_CASE("An import is not offered without a source to import", "[plugin][import]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);

	RecordingHost host;
	REQUIRE_FALSE(ImportAction(registry).action->IsEnabled(host, {}));

	// Invoked anyway -- an action reached by a shortcut is not asked first -- it must not import.
	ImportAction(registry).action->Invoke(host, {});
	REQUIRE(host.imported.empty());
}

TEST_CASE("Translated labels preserve menu routing and action identity", "[plugin][localization]")
{
	auto              plugin = sample::CreateEditorPlugin();
	RecordingRegistry registry;
	plugin->Register(registry);
	RecordingHost host;
	REQUIRE(registry.catalogs.size() == 1);
	host.language.RegisterCatalog(registry.catalogs.front());
	const auto resolve = [&host](const editor::LocalizedText& text) {
		return text.Resolve(host.GetLanguageResolver());
	};
	const auto& menu   = registry.menus.front();
	const auto& action = registry.actions.front();
	const auto& panel  = registry.panels.front();
	REQUIRE(resolve(menu.title) == "Sample tools");
	REQUIRE(resolve(action.title) == "Project tools");
	host.language.RegisterCatalog(
		{ "other.editor", { { "document", "zh_CN", "Unrelated translation" } } });
	host.language.SetLocale("zh_CN");
	REQUIRE(resolve(menu.title) == QString::fromUtf8("示例工具"));
	REQUIRE(resolve(action.title) == QString::fromUtf8("项目工具"));
	REQUIRE(resolve(panel.title) == resolve(action.title));
	REQUIRE(resolve(registry.editors.front().title) == "Sample document");
	REQUIRE(action.menuId == menu.id);
	REQUIRE(menu.parentId == editor::c_ToolsMenuId);
	REQUIRE(action.id == "sample.show-overview");
	action.action->Invoke(host, {});
	REQUIRE(host.shown == std::vector<std::string>{ panel.id });
	QWidget     root;
	const auto* widget = panel.factory->Create(host, &root);
	REQUIRE(widget->findChild<QLabel*>()->text() == resolve(panel.title));
	host.language.SetLocale("en");
	REQUIRE(resolve(action.title) == "Project tools");
	REQUIRE(action.menuId == menu.id);
}

TEST_CASE("Owned actions survive registration storage growth and movement", "[plugin][lifetime]")
{
	struct State
	{
		int destroyed = 0;
	};
	class Action final : public editor::IEditorAction
	{
	public:
		Action(std::shared_ptr<State> state, std::string id) :
			m_State(std::move(state)), m_Id(std::move(id))
		{}
		~Action() override { ++m_State->destroyed; }
		bool
		IsEnabled(editor::IEditorHost&, std::span<const std::string>) const override
		{
			return true;
		}
		void
		Invoke(editor::IEditorHost& host, std::span<const std::string>) override
		{
			host.ShowPanel(m_Id);
		}

	private:
		std::shared_ptr<State> m_State;
		std::string            m_Id;
	};
	static_assert(!std::is_move_constructible_v<Action>);
	static_assert(!std::is_copy_constructible_v<editor::ActionDesc>);
	auto          state = std::make_shared<State>();
	RecordingHost host;
	{
		RecordingRegistry registry;
		auto              desc =
			editor::ActionDesc().SetId("sample.first").AddAction<Action>(state, "sample.original");
		auto* original = desc.action.get();
		registry.AddAction(std::move(desc));
		const auto oldCapacity = registry.actions.capacity();
		for (int i = 0; i < 128; ++i)
			registry.AddAction(
				editor::ActionDesc().SetId("sample.next").AddAction<Action>(state, "sample.next"));
		CHECK(registry.actions.capacity() > oldCapacity);
		CHECK(registry.actions.front().action.get() == original);
		RecordingRegistry moved(std::move(registry));
		original->Invoke(host, {});
		CHECK(host.shown == std::vector<std::string>{ "sample.original" });
		CHECK(state->destroyed == 0);
	}
	CHECK(state->destroyed == 129);
}

TEST_CASE(
	"Descriptor builders forward ownership and preserve it when construction fails",
	"[plugin][lifetime]")
{
	class Factory final : public editor::IEditorPanelFactory
	{
	public:
		explicit Factory(std::unique_ptr<std::string> value) : m_Value(std::move(value))
		{
			if (!m_Value)
				throw std::runtime_error("Missing configuration");
		}
		editor::EditorPanel*
		Create(editor::IEditorHost&, QWidget*) override
		{
			return nullptr;
		}
		const std::string&
		GetValue() const noexcept
		{
			return *m_Value;
		}

	private:
		std::unique_ptr<std::string> m_Value;
	};
	auto value = std::make_unique<std::string>("owned configuration");
	auto desc  = editor::PanelDesc().SetId("sample.panel").AddFactory<Factory>(std::move(value));
	CHECK(value == nullptr);
	const auto* factory = static_cast<const Factory*>(desc.factory.get());
	CHECK(factory->GetValue() == "owned configuration");
	CHECK(&desc.SetTitle({ "sample.editor", "panel", "Panel" }) == &desc);
	CHECK_THROWS(desc.AddFactory<Factory>(nullptr));
	CHECK(desc.factory.get() == factory);
	CHECK(factory->GetValue() == "owned configuration");
	RecordingRegistry registry;
	registry.AddPanel(std::move(desc));
	CHECK(desc.factory == nullptr);
	CHECK(registry.panels.front().factory.get() == factory);
}
