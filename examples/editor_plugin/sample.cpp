#include "sample.h"

#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <assetlib/IAssetPlugin.h>
#include <charconv>
#include <cstddef>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/IEditorRegistry.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
		assetlib::AssetKindDesc m_Desc{ "sample.document",
			                            ".bexample",
			                            assetlib::DocumentPacking::kInclude };
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

	class OverviewPanel final : public editor::EditorPanel
	{
	public:
		explicit OverviewPanel(QWidget* parent) : EditorPanel(parent)
		{
			auto* layout = new QVBoxLayout(this);
			layout->addWidget(new QLabel("Project tools", this));
		}

		std::vector<std::string>
		GetHeldAssets() const override
		{
			return {};
		}

		bool
		CanClose() override
		{
			return true;
		}

		void
		SetActive(bool active) override
		{
			setEnabled(active);
		}
	};

	class DocumentPanel final : public editor::AssetEditorPanel
	{
	public:
		explicit DocumentPanel(QWidget* parent) : AssetEditorPanel(parent)
		{
			auto* layout = new QVBoxLayout(this);
			m_Label      = new QLabel(this);
			layout->addWidget(m_Label);
		}

		void
		OpenAsset(std::string_view key) override
		{
			m_Key = key;
			m_Label->setText(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
		}

		std::vector<std::string>
		GetHeldAssets() const override
		{
			return m_Key.empty() ? std::vector<std::string>{} : std::vector<std::string>{ m_Key };
		}

		bool
		CanClose() override
		{
			return true;
		}

		void
		SetActive(bool active) override
		{
			setEnabled(active);
		}

	private:
		QLabel*     m_Label = nullptr;
		std::string m_Key;
	};

	class SampleEditorPlugin final : public editor::IEditorPlugin
	{
	public:
		void
		Register(editor::IEditorRegistry& registry) override
		{
			registry.AddPanel(
				{ "sample.overview", "Project tools", [](editor::IEditorHost&, QWidget* parent) {
					 return new OverviewPanel(parent);
				 } });
			registry.AddAssetEditor(
				{ "sample.document",
			      "Sample document",
			      { ".bexample" },
			      [](editor::IEditorHost&, QWidget* parent) {
					  return new DocumentPanel(parent);
				  } });
			registry.AddAction(
				{ "sample.show-overview",
			      "Project tools",
			      { "Tools" },
			      {},
			      [](editor::IEditorHost&, std::span<const std::string>) { return true; },
			      [](editor::IEditorHost& host, std::span<const std::string>) {
					  host.ShowPanel("sample.overview");
				  } });
		}
	};
}

namespace sample
{
	std::unique_ptr<assetlib::IAssetPlugin>
	CreateAssetPlugin()
	{
		return std::make_unique<SampleAssetPlugin>();
	}

	std::unique_ptr<editor::IEditorPlugin>
	CreateEditorPlugin()
	{
		return std::make_unique<SampleEditorPlugin>();
	}
}
