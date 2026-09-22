#include "runtime.h"

#include <assetlib/IAssetPlugin.h>
#include <charconv>
#include <cstddef>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	nlohmann::json
	ReadDocument(std::span<const std::byte> bytes)
	{
		auto document = nlohmann::json::parse(bytes.begin(), bytes.end());
		if (!document.is_object() || !document.contains("references") ||
		    !document.at("references").is_array())
			throw std::runtime_error("A sample document requires a references array");
		for (const auto& reference : document.at("references"))
			if (!reference.is_string() || reference.get_ref<const std::string&>().empty())
				throw std::runtime_error("A sample reference must be a nonempty mount key");
		return document;
	}

	std::vector<std::byte>
	Encode(const nlohmann::json& document)
	{
		const auto text  = document.dump();
		const auto bytes = std::as_bytes(std::span(text));
		return { bytes.begin(), bytes.end() };
	}

	class SampleKind final : public assetlib::IAssetKind
	{
	public:
		const assetlib::AssetKindDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		std::vector<assetlib::DocumentReference>
		ReadReferences(std::span<const std::byte> bytes) const override
		{
			const auto                               document = ReadDocument(bytes);
			std::vector<assetlib::DocumentReference> references;
			for (std::size_t index = 0; index < document.at("references").size(); ++index)
				references.push_back(
					{ document.at("references").at(index).get<std::string>(),
				      std::to_string(index) });
			return references;
		}

		std::vector<std::byte>
		RewriteReferences(
			std::span<const std::byte>                   bytes,
			std::span<const assetlib::DocumentReference> replacements) const override
		{
			auto document = ReadDocument(bytes);
			for (const auto& replacement : replacements)
			{
				std::size_t index  = 0;
				const auto* end    = replacement.field.data() + replacement.field.size();
				const auto  parsed = std::from_chars(replacement.field.data(), end, index);
				if (parsed.ec != std::errc{} || parsed.ptr != end ||
				    index >= document.at("references").size() ||
				    std::to_string(index) != replacement.field)
					throw std::runtime_error("Unknown sample reference field");
				if (replacement.target.empty())
					throw std::runtime_error("A sample reference must be a nonempty mount key");
				document.at("references").at(index) = replacement.target;
			}
			return Encode(document);
		}

		std::vector<std::byte>
		Migrate(std::span<const std::byte> bytes) const override
		{
			return Encode(ReadDocument(bytes));
		}

	private:
		assetlib::AssetKindDesc m_Desc{ "sample.document", ".bexample", true };
	};

	class SampleAssetPlugin final : public assetlib::IAssetPlugin
	{
	public:
		void
		RegisterKinds(assetlib::IAssetKindRegistry& registry) override
		{
			registry.Add(std::make_unique<SampleKind>());
		}
	};

}

namespace sample
{
	assetlib::AssetPluginPtr
	CreateAssetPlugin()
	{
		return std::make_unique<SampleAssetPlugin>();
	}

}
