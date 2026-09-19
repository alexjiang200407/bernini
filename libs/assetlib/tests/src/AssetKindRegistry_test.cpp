#include <assetlib/AssetKindRegistry.h>
#include <assetlib/IAssetPlugin.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
	class TestKind final : public assetlib::IAssetKind
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
}

TEST_CASE("Asset kind registry owns valid custom kinds", "[plugins][assetkind]")
{
	assetlib::AssetKindRegistry registry;
	registry.Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	REQUIRE(registry.Kinds().size() == 1);
	REQUIRE(registry.FindById("sample.document") != nullptr);
	REQUIRE(registry.FindByExtension(".bexample") == registry.FindById("sample.document"));
}

TEST_CASE(
	"Asset kind registry rejects invalid, built-in and duplicate claims",
	"[plugins][assetkind]")
{
	assetlib::AssetKindRegistry registry;
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("", ".bexample")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad key", ".custom")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad.extension", "custom")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("bad.extension", ".bad-ext")));
	REQUIRE_THROWS(registry.Add(nullptr));
	registry.Add(std::make_unique<TestKind>("sample.document", ".bexample"));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("other.document", ".bexample")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("sample.document", ".other")));
	REQUIRE_THROWS(registry.Add(std::make_unique<TestKind>("mesh.document", ".bmesh")));
}
