#include "Plugins/EditorHost.h"
#include "Plugins/EditorRegistry.h"
#include "Plugins/plugin_loader.h"

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/asset_import.h>  // IWYU pragma: keep
#include <assetlib/asset_refs.h>
#include <assetlib/migrate.h>
#include <assetlib/pak.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IAssetEditorFactory.h>
#include <editor_plugin_api/IEditorAction.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(EDITOR_PLUGIN_SAMPLE_DIR)
namespace
{
	namespace fs = std::filesystem;

	struct SampleProject
	{
		fs::path root = fs::temp_directory_path() / "bernini_external_plugin";
		SampleProject()
		{
			fs::remove_all(root);
			assetlib::Project::Create(root / "Sample.bproj", "Sample");
			const auto file     = root / "Sample.bproj";
			auto       metadata = nlohmann::json::parse(std::ifstream(file));
			metadata["plugins"] = { "sample.document" };
			std::ofstream(file) << metadata.dump();
			std::ofstream(root / "Data/Target.bexample") << R"({ "references": [] })";
			std::ofstream(root / "Data/Holder.bexample")
				<< R"({ "references": ["Target.bexample", "Target.bexample"], "future": {"value": 42} })";
		}
		~SampleProject()
		{
			std::error_code error;
			fs::remove_all(root, error);
		}
	};
}

TEST_CASE(
	"An independently built plugin carries authored references into a runtime archive",
	"[plugins][sample]")
{
	SampleProject sandbox;
	const auto    projectFile = sandbox.root / "Sample.bproj";
	auto          session     = editor::plugins::PluginSession::Load(
		std::vector<fs::path>{ EDITOR_PLUGIN_SAMPLE_DIR },
		editor::plugins::CurrentBuildIdentity(),
		sandbox.root / "copies");
	session.RegisterEditorPlugins();
	CHECK(
		editor::plugins::MissingRequiredPlugins(
			session.Ids(),
			assetlib::Project::PluginIdsOf(projectFile))
			.empty());
	const auto  project = assetlib::Project::Open(projectFile, session.KindRegistry());
	const auto& store   = project.GetStore();
	const auto  graph   = assetlib::AssetRefGraph::Scan(store);
	REQUIRE(graph.ReferrersOf("Target.bexample").size() == 1);
	CHECK_FALSE(assetlib::planDeletion(graph, "Target.bexample").Allowed());

	const auto rename = assetlib::planRename(graph, "Target.bexample", "Renamed.bexample");
	REQUIRE(store.RenameAsset(rename).status == assetlib::RenameStatus::kRenamed);
	CHECK_FALSE(store.Exists("Target.bexample"));
	const auto bytes    = store.GetFiles().Read("Holder.bexample");
	const auto document = nlohmann::json::parse(bytes.begin(), bytes.end());
	CHECK(
		document.at("references") ==
		nlohmann::json::array({ "Renamed.bexample", "Renamed.bexample" }));
	CHECK(document.at("future").at("value") == 42);
	const auto migrated = store.Migrate(false);
	CHECK(migrated.Count(assetlib::MigratedFile::Outcome::kFailed) == 0);
	const auto current = store.GetFiles().Read("Holder.bexample");
	CHECK(nlohmann::json::parse(current.begin(), current.end()) == document);

	const auto archivePath = sandbox.root / "Data.bpak";
	REQUIRE(store.Pack({ archivePath }).entries == 2);
	fs::remove_all(project.GetDataDirectory());
	const assetlib::AssetStore archived(
		{},
		std::make_shared<assetlib::PakFile>(archivePath),
		session.KindRegistry());
	CHECK(archived.IsReadOnly());
	CHECK(archived.GetFiles().Read("Holder.bexample") == current);

	QProcess reader;
	reader.start(
		QString::fromUtf8(EDITOR_PLUGIN_SAMPLE_READER),
		QStringList{ QString::fromStdWString(archivePath.wstring()), "Holder.bexample" });
	REQUIRE(reader.waitForStarted());
	REQUIRE(reader.waitForFinished());
	INFO(reader.readAllStandardError().toStdString());
	REQUIRE(reader.exitStatus() == QProcess::NormalExit);
	REQUIRE(reader.exitCode() == 0);
	CHECK(
		reader.readAllStandardOutput().replace("\r\n", "\n") ==
		"Renamed.bexample\nRenamed.bexample\n");
}

TEST_CASE(
	"The independently built sample creates project-owned tabs through the host",
	"[plugins][sample]")
{
	SampleProject sandbox;
	auto          session = editor::plugins::PluginSession::Load(
		std::vector<fs::path>{ EDITOR_PLUGIN_SAMPLE_DIR },
		editor::plugins::CurrentBuildIdentity(),
		sandbox.root / "copies");
	session.RegisterEditorPlugins();
	const auto project =
		assetlib::Project::Open(sandbox.root / "Sample.bproj", session.KindRegistry());
	const auto&                 registry = session.Contributions();
	std::string                 shown;
	editor::plugins::EditorHost host(
		project.GetStore(),
		registry.Catalogs(),
		nullptr,
		nullptr,
		true,
		{ .showPanel = [&shown](std::string_view id) { shown = id; } });
	QWidget parent;
	REQUIRE(registry.Actions().size() == 1);
	registry.Actions().front().action->Invoke(host, {});
	CHECK(shown == "sample.overview");
	const auto* overview = registry.FindPanel(shown);
	REQUIRE(overview != nullptr);
	auto* panel = overview->factory->Create(host, &parent);
	REQUIRE(panel != nullptr);
	CHECK(panel->parentWidget() == &parent);
	panel->SetActive(true);
	CHECK(panel->CanClose());

	const auto* descriptor = registry.FindAssetEditor(".bexample");
	REQUIRE(descriptor != nullptr);
	auto* document = descriptor->factory->Create(host, &parent);
	REQUIRE(document != nullptr);
	CHECK(document->parentWidget() == &parent);
	document->OpenAsset("Holder.bexample");
	CHECK(document->GetHeldAssets() == std::vector<std::string>{ "Holder.bexample" });
	document->OpenAsset("Target.bexample");
	CHECK(document->GetHeldAssets() == std::vector<std::string>{ "Target.bexample" });
	CHECK(document->CanClose());
}
#endif
