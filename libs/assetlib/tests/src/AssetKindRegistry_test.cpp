#include <assetlib/AssetKindRegistry.h>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <assetlib/asset_refs.h>

#include <catch2/catch_test_macros.hpp>
#include <core/file/LooseFileSystem.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class TestKind : public assetlib::IAssetKind
	{
	public:
		explicit TestKind(std::string id, std::string extension) :
			m_Desc{ std::move(id), std::move(extension), true }
		{}

		const assetlib::AssetKindDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}
		std::vector<assetlib::DocumentReference>
		ReadReferences(std::span<const std::byte>) const override
		{
			return {};
		}
		std::vector<std::byte>
		RewriteReferences(
			std::span<const std::byte> bytes,
			std::span<const assetlib::DocumentReference>) const override
		{
			return { bytes.begin(), bytes.end() };
		}
		std::vector<std::byte>
		Migrate(std::span<const std::byte> bytes) const override
		{
			return { bytes.begin(), bytes.end() };
		}

	private:
		assetlib::AssetKindDesc m_Desc;
	};

	class ReferencingKind final : public TestKind
	{
	public:
		ReferencingKind() : TestKind("sample.reference", ".bref") {}

		std::vector<assetlib::DocumentReference>
		ReadReferences(std::span<const std::byte>) const override
		{
			return { { "Target.bexample", "target" } };
		}

		std::vector<std::byte>
		RewriteReferences(
			std::span<const std::byte>,
			std::span<const assetlib::DocumentReference> replacements) const override
		{
			const std::string value =
				replacements.empty() ? std::string() : replacements.front().target;
			return { reinterpret_cast<const std::byte*>(value.data()),
				     reinterpret_cast<const std::byte*>(value.data() + value.size()) };
		}
	};

	assetlib::AssetStore
	MakeStore(
		const std::filesystem::path&                              root,
		const std::shared_ptr<const assetlib::AssetKindRegistry>& registry)
	{
		return assetlib::AssetStore(
			root,
			std::make_shared<const core::file::LooseFileSystem>(root),
			registry);
	}
}

TEST_CASE("Asset kind registry owns valid custom kinds", "[plugins][assetkind]")
{
	auto registry = std::make_shared<assetlib::AssetKindRegistry>();
	registry->Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	REQUIRE(registry->Kinds().size() == 1);
	REQUIRE(registry->FindById("sample.document") != nullptr);
	REQUIRE(registry->FindByExtension(".bexample") == registry->FindById("sample.document"));
	const auto root = std::filesystem::temp_directory_path() / "bernini_asset_kind_registry";
	std::filesystem::create_directories(root);
	assetlib::AssetStore store = MakeStore(root, registry);
	REQUIRE(store.GetKindRegistry() == registry);
	std::filesystem::remove_all(root);
}

TEST_CASE(
	"Asset kind registry rewrites custom references from execution bytes",
	"[plugins][assetkind]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_asset_kind_rename";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	std::ofstream(root / "Target.bexample") << "target";
	std::ofstream(root / "Holder.bref") << "Target.bexample";

	auto registry = std::make_shared<assetlib::AssetKindRegistry>();
	registry->Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	registry->Add(std::make_unique<ReferencingKind>());
	assetlib::AssetStore store = MakeStore(root, registry);
	auto                 plan  = assetlib::planRename(
		assetlib::AssetRefGraph::Scan(store),
		"Target.bexample",
		"Renamed.bexample");
	std::ofstream(root / "Holder.bref", std::ios::trunc) << "Target.bexample";
	REQUIRE(store.RenameAsset(plan).status == assetlib::RenameStatus::kRenamed);
	std::ifstream holder(root / "Holder.bref");
	std::string   value;
	holder >> value;
	REQUIRE(value == "Renamed.bexample");

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"Asset kind registry feeds custom references into the asset graph",
	"[plugins][assetkind]")
{
	const auto root = std::filesystem::temp_directory_path() / "bernini_asset_kind_graph";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	std::ofstream(root / "Target.bexample") << "target";
	std::ofstream(root / "Holder.bref") << "holder";

	auto registry = std::make_shared<assetlib::AssetKindRegistry>();
	registry->Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	registry->Add(std::make_unique<ReferencingKind>());
	assetlib::AssetStore          store = MakeStore(root, registry);
	const assetlib::AssetRefGraph graph = assetlib::AssetRefGraph::Scan(store);
	REQUIRE(graph.ReferrersOf("Target.bexample").size() == 1);
	REQUIRE(graph.ReferrersOf("Target.bexample").front().kind == assetlib::RefKind::kPlugin);
	REQUIRE_FALSE(assetlib::planDeletion(graph, "Target.bexample").Allowed());

	std::filesystem::remove_all(root);
}

TEST_CASE(
	"Asset kind registry rejects invalid, built-in and duplicate claims",
	"[plugins][assetkind]")
{
	assetlib::AssetKindRegistry registry;
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("", ".bexample")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad key", ".custom")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("upper.extension", ".BEXAMPLE")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad.extension", "custom")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad.extension", ".bad-ext")));
	REQUIRE_THROWS(registry.Add(nullptr));
	registry.Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("other.document", ".bexample")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("sample.document", ".other")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("mesh.document", ".bmesh")));
}
