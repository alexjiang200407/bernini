#pragma once

#include <editor_api/IAssetEditorFactory.h>
#include <editor_api/IEditorAction.h>
#include <editor_api/IEditorImporter.h>
#include <editor_api/IEditorPanelFactory.h>
#include <editor_api/IThumbnailProvider.h>
#include <editor_api/LocalizedText.h>
#include <editor_api/TranslationCatalog.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	inline constexpr std::string_view c_FileMenuId  = "editor.file";
	inline constexpr std::string_view c_ToolsMenuId = "editor.tools";

	struct MenuDesc
	{
		std::string   id;
		std::string   parentId;
		LocalizedText title;
	};

	struct PanelDesc
	{
		std::string                          id;
		LocalizedText                        title;
		std::unique_ptr<IEditorPanelFactory> factory;
	};

	struct AssetEditorDesc
	{
		std::string                          id;
		LocalizedText                        title;
		std::vector<std::string>             extensions;
		std::unique_ptr<IAssetEditorFactory> factory;
	};

	struct ActionDesc
	{
		std::string                    id;
		LocalizedText                  title;
		std::string                    menuId;
		std::vector<std::string>       extensions;
		std::unique_ptr<IEditorAction> action;
	};

	struct ImporterDesc
	{
		std::string                      id;
		std::vector<std::string>         extensions;
		std::unique_ptr<IEditorImporter> importer;
	};

	struct ThumbnailProviderDesc
	{
		std::string                         id;
		std::vector<std::string>            extensions;
		std::unique_ptr<IThumbnailProvider> provider;
	};

	// Startup only; ownership transfers even on rejection. See docs/editor_plugins.md.
	class IEditorRegistry
	{
	public:
		virtual ~IEditorRegistry() = default;

		/** Own the module catalog by value; one per context, rolled back with failed registration. */
		virtual void
		AddTranslations(TranslationCatalog catalog) = 0;

		/** Parent must already exist; empty parent creates a root menu. Built-in menu IDs are reserved. */
		virtual void
		AddMenu(MenuDesc desc) = 0;

		/** Factories run lazily on the GUI thread; return a non-null child of the supplied parent. */
		virtual void
		AddPanel(PanelDesc desc) = 0;

		/** One opener per extension. Editor IDs share the panel ID namespace. */
		virtual void
		AddAssetEditor(AssetEditorDesc desc) = 0;

		/** Requires an open project; empty extensions means a menu action, otherwise all selected assets must match. */
		virtual void
		AddAction(ActionDesc desc) = 0;

		/** One importer per source extension; Import receives an OS source path and a target folder key. */
		virtual void
		AddImporter(ImporterDesc desc) = 0;

		/** One provider per extension; Describe may run concurrently on workers and must not touch widgets. */
		virtual void
		AddThumbnailProvider(ThumbnailProviderDesc desc) = 0;
	};
}
