#include "sample.h"

#include <QLabel>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>
#include <editor_api/IEditorRegistry.h>
#include <editor_api/IEditorViewport.h>
#include <exception>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	class RecordingRegistry final :
		public editor::IEditorRegistry,
		public assetlib::IAssetKindRegistry
	{
	public:
		std::vector<editor::PanelDesc>             panels;
		std::vector<editor::AssetEditorDesc>       editors;
		std::vector<editor::ActionDesc>            actions;
		std::vector<editor::ImporterDesc>          importers;
		std::vector<editor::ThumbnailProviderDesc> thumbnails;
		std::vector<assetlib::AssetKindPtr>        kinds;

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
		OpenAsset(std::string_view) override
		{
			throw std::runtime_error("Unexpected asset dispatch");
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
	REQUIRE(registry.actions.size() == 1);
	REQUIRE(registry.actions.front().extensions.empty());
	REQUIRE(registry.actions.front().menu == QStringList{ "Tools" });

	RecordingHost host;
	QWidget       root;
	const auto&   desc  = registry.panels.front();
	auto*         panel = desc.create(host, &root);
	REQUIRE(panel->parentWidget() == &root);
	REQUIRE(panel->GetHeldAssets().empty());
	REQUIRE(panel->CanClose());
	REQUIRE(registry.actions.front().enabled(host, {}));
	registry.actions.front().invoke(host, {});
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
		observed = desc.create(host, &root);
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
	registry.actions.front().invoke(host, {});
	REQUIRE(host.shown.size() == 1);
	RecordingHost nextHost;
	QWidget       nextRoot;
	auto*         nextPanel = registry.editors.front().create(nextHost, &nextRoot);
	REQUIRE(nextPanel->GetHeldAssets().empty());
	registry.actions.front().invoke(nextHost, {});
	REQUIRE(nextHost.shown == host.shown);
	REQUIRE(host.shown.size() == 1);
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
	REQUIRE(kind.GetDesc().packing == assetlib::DocumentPacking::kInclude);
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
