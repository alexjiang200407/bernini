#pragma once

#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <span>
#include <string_view>
#include <vector>

namespace editor::plugins
{
	class EditorRegistry final : public IEditorRegistry
	{
	public:
		/**
		 * Startup only. `catalogs` are the plugin's discovered CSVs, added as if it had registered
		 * them itself; a failed registration destroys every contribution and preserves earlier ones.
		 */
		void
		Register(IEditorPlugin& plugin, std::vector<TranslationCatalog> catalogs = {});

		void
		AddTranslations(TranslationCatalog catalog) override;

		void
		AddMenu(MenuDesc desc) override;

		void
		AddPanel(PanelDesc desc) override;

		void
		AddAssetEditor(AssetEditorDesc desc) override;

		void
		AddAction(ActionDesc desc) override;

		void
		AddImporter(ImporterDesc desc) override;

		void
		AddThumbnailProvider(ThumbnailProviderDesc desc) override;

		[[nodiscard]] std::span<const TranslationCatalog>
		Catalogs() const noexcept;

		[[nodiscard]] std::span<const MenuDesc>
		Menus() const noexcept;

		[[nodiscard]] std::span<const PanelDesc>
		Panels() const noexcept;

		[[nodiscard]] std::span<const AssetEditorDesc>
		AssetEditors() const noexcept;

		[[nodiscard]] std::span<const ActionDesc>
		Actions() const noexcept;

		[[nodiscard]] std::span<const ImporterDesc>
		Importers() const noexcept;

		[[nodiscard]] std::span<const ThumbnailProviderDesc>
		ThumbnailProviders() const noexcept;

		[[nodiscard]] const PanelDesc*
		FindPanel(std::string_view id) const noexcept;

		[[nodiscard]] const AssetEditorDesc*
		FindAssetEditor(std::string_view extension) const noexcept;

		[[nodiscard]] const ImporterDesc*
		FindImporter(std::string_view extension) const noexcept;

		[[nodiscard]] const ThumbnailProviderDesc*
		FindThumbnailProvider(std::string_view extension) const noexcept;

	private:
		std::vector<TranslationCatalog>    m_Catalogs;
		std::vector<MenuDesc>              m_Menus;
		std::vector<PanelDesc>             m_Panels;
		std::vector<AssetEditorDesc>       m_AssetEditors;
		std::vector<ActionDesc>            m_Actions;
		std::vector<ImporterDesc>          m_Importers;
		std::vector<ThumbnailProviderDesc> m_ThumbnailProviders;
	};
}
