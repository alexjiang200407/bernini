#pragma once

#include <QWidget>
#include <assetlib/AssetStore.h>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>
#include <editor_api/LocalizedText.h>
#include <editor_api/Thumbnail.h>
#include <editor_api/TranslationCatalog.h>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	using PanelFactory       = std::function<EditorPanel*(IEditorHost&, QWidget*)>;
	using AssetEditorFactory = std::function<AssetEditorPanel*(IEditorHost&, QWidget*)>;
	using ActionPredicate    = std::function<bool(IEditorHost&, std::span<const std::string>)>;
	using ActionCallback     = std::function<void(IEditorHost&, std::span<const std::string>)>;
	using ImportCallback =
		std::function<void(IEditorHost&, const std::filesystem::path&, std::string_view)>;
	using ThumbnailCallback =
		std::function<Thumbnail(const assetlib::AssetStore&, std::string_view)>;

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
		std::string   id;
		LocalizedText title;
		PanelFactory  create;
	};

	struct AssetEditorDesc
	{
		std::string              id;
		LocalizedText            title;
		std::vector<std::string> extensions;
		AssetEditorFactory       create;
	};

	struct ActionDesc
	{
		std::string              id;
		LocalizedText            title;
		std::string              menuId;
		std::vector<std::string> extensions;
		ActionPredicate          enabled;
		ActionCallback           invoke;
	};

	struct ImporterDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		ImportCallback           importAsset;
	};

	struct ThumbnailProviderDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		ThumbnailCallback        describe;
	};

	/** Startup only. Own descriptors by value; reject invalid IDs/callbacks and collisions. See docs/editor_plugins.md. */
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

		/** One importer per source extension; callback receives an OS source path and a target folder key. */
		virtual void
		AddImporter(ImporterDesc desc) = 0;

		/** One provider per extension; describe may run concurrently on workers and must not touch widgets. */
		virtual void
		AddThumbnailProvider(ThumbnailProviderDesc desc) = 0;
	};
}
