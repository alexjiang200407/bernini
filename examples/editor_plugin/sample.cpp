#include "sample.h"

#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IAssetEditorFactory.h>
#include <editor_plugin_api/IEditorAction.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/LocalizedText.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	class OverviewPanel final : public editor::EditorPanel
	{
	public:
		explicit OverviewPanel(editor::IEditorHost& host, QWidget* parent) : EditorPanel(parent)
		{
			auto*                       layout = new QVBoxLayout(this);
			const editor::LocalizedText title{ "sample.editor", "overview", "Project tools" };
			layout->addWidget(new QLabel(title.Resolve(host.GetLanguageResolver()), this));
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
			m_Changed = new QLabel(this);
			m_Changed->setObjectName("sample.changedAsset");
			layout->addWidget(m_Changed);
		}

		void
		OpenAsset(std::string_view key) override
		{
			m_Key = key;
			m_Changed->clear();
			m_Label->setText(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
		}

		void
		OnAssetChanged(std::string_view key) override
		{
			if (key == m_Key)
				m_Changed->setText(
					QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
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
		QLabel*     m_Label   = nullptr;
		QLabel*     m_Changed = nullptr;
		std::string m_Key;
	};

	class OverviewFactory final : public editor::IEditorPanelFactory
	{
	public:
		editor::EditorPanel*
		Create(editor::IEditorHost& host, QWidget* parent) override
		{
			return new OverviewPanel(host, parent);
		}
	};
	class DocumentFactory final : public editor::IAssetEditorFactory
	{
	public:
		editor::AssetEditorPanel*
		Create(editor::IEditorHost&, QWidget* parent) override
		{
			return new DocumentPanel(parent);
		}
	};
	class OpenPanelAction final : public editor::IEditorAction
	{
	public:
		explicit OpenPanelAction(std::string panelId) : m_PanelId(std::move(panelId)) {}
		bool
		IsEnabled(editor::IEditorHost&, std::span<const std::string>) const override
		{
			return true;
		}
		void
		Invoke(editor::IEditorHost& host, std::span<const std::string>) override
		{
			host.ShowPanel(m_PanelId);
		}

	private:
		std::string m_PanelId;
	};

	class SampleEditorPlugin final : public editor::IEditorPlugin
	{
	public:
		void
		Register(editor::IEditorRegistry& registry) override
		{
			registry.AddTranslations(
				{ "sample.editor",
			      { { "overview", "zh_CN", QString::fromUtf8("项目工具") },
			        { "tools", "zh_CN", QString::fromUtf8("示例工具") } } });
			registry.AddMenu(
				editor::MenuDesc()
					.SetId("sample.tools")
					.SetParentId(std::string(editor::c_ToolsMenuId))
					.SetTitle({ "sample.editor", "tools", "Sample tools" }));
			registry.AddPanel(
				editor::PanelDesc()
					.SetId("sample.overview")
					.SetTitle({ "sample.editor", "overview", "Project tools" })
					.AddFactory<OverviewFactory>());
			registry.AddAssetEditor(
				editor::AssetEditorDesc()
					.SetId("sample.document")
					.SetTitle({ "sample.editor", "document", "Sample document" })
					.AddExtension(".bexample")
					.AddFactory<DocumentFactory>());
			registry.AddAction(
				editor::ActionDesc()
					.SetId("sample.show-overview")
					.SetTitle({ "sample.editor", "overview", "Project tools" })
					.SetMenuId("sample.tools")
					.AddAction<OpenPanelAction>("sample.overview"));
		}
	};
}

namespace sample
{
	editor::EditorPluginPtr
	CreateEditorPlugin()
	{
		return std::make_unique<SampleEditorPlugin>();
	}
}
