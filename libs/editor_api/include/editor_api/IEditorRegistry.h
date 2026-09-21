#pragma once

#include <concepts>
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
#include <utility>
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
		MenuDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		MenuDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		MenuDesc&
		SetParentId(std::string value) &
		{
			parentId = std::move(value);
			return *this;
		}

		MenuDesc&&
		SetParentId(std::string value) &&
		{
			SetParentId(std::move(value));
			return std::move(*this);
		}

		MenuDesc&
		SetTitle(LocalizedText value) &
		{
			title = std::move(value);
			return *this;
		}

		MenuDesc&&
		SetTitle(LocalizedText value) &&
		{
			SetTitle(std::move(value));
			return std::move(*this);
		}
	};

	struct PanelDesc
	{
		std::string           id;
		LocalizedText         title;
		EditorPanelFactoryPtr factory;
		PanelDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		PanelDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		PanelDesc&
		SetTitle(LocalizedText value) &
		{
			title = std::move(value);
			return *this;
		}

		PanelDesc&&
		SetTitle(LocalizedText value) &&
		{
			SetTitle(std::move(value));
			return std::move(*this);
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorPanelFactory> &&
		             std::constructible_from<T, Args...>
		PanelDesc&
		AddFactory(Args&&... args) &
		{
			factory = std::make_unique<T>(std::forward<Args>(args)...);
			return *this;
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorPanelFactory> &&
		             std::constructible_from<T, Args...>
		PanelDesc&&
		AddFactory(Args&&... args) &&
		{
			AddFactory<T>(std::forward<Args>(args)...);
			return std::move(*this);
		}
	};

	struct AssetEditorDesc
	{
		std::string              id;
		LocalizedText            title;
		std::vector<std::string> extensions;
		AssetEditorFactoryPtr    factory;
		AssetEditorDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		AssetEditorDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		AssetEditorDesc&
		SetTitle(LocalizedText value) &
		{
			title = std::move(value);
			return *this;
		}

		AssetEditorDesc&&
		SetTitle(LocalizedText value) &&
		{
			SetTitle(std::move(value));
			return std::move(*this);
		}

		AssetEditorDesc&
		AddExtension(std::string extension) &
		{
			extensions.push_back(std::move(extension));
			return *this;
		}

		AssetEditorDesc&&
		AddExtension(std::string extension) &&
		{
			AddExtension(std::move(extension));
			return std::move(*this);
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IAssetEditorFactory> &&
		             std::constructible_from<T, Args...>
		AssetEditorDesc&
		AddFactory(Args&&... args) &
		{
			factory = std::make_unique<T>(std::forward<Args>(args)...);
			return *this;
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IAssetEditorFactory> &&
		             std::constructible_from<T, Args...>
		AssetEditorDesc&&
		AddFactory(Args&&... args) &&
		{
			AddFactory<T>(std::forward<Args>(args)...);
			return std::move(*this);
		}
	};

	struct ActionDesc
	{
		std::string              id;
		LocalizedText            title;
		std::string              menuId;
		std::vector<std::string> extensions;
		EditorActionPtr          action;
		ActionDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		ActionDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		ActionDesc&
		SetTitle(LocalizedText value) &
		{
			title = std::move(value);
			return *this;
		}

		ActionDesc&&
		SetTitle(LocalizedText value) &&
		{
			SetTitle(std::move(value));
			return std::move(*this);
		}

		ActionDesc&
		SetMenuId(std::string value) &
		{
			menuId = std::move(value);
			return *this;
		}

		ActionDesc&&
		SetMenuId(std::string value) &&
		{
			SetMenuId(std::move(value));
			return std::move(*this);
		}

		ActionDesc&
		AddExtension(std::string extension) &
		{
			extensions.push_back(std::move(extension));
			return *this;
		}

		ActionDesc&&
		AddExtension(std::string extension) &&
		{
			AddExtension(std::move(extension));
			return std::move(*this);
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorAction> && std::constructible_from<T, Args...>
		ActionDesc&
		AddAction(Args&&... args) &
		{
			action = std::make_unique<T>(std::forward<Args>(args)...);
			return *this;
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorAction> && std::constructible_from<T, Args...>
		ActionDesc&&
		AddAction(Args&&... args) &&
		{
			AddAction<T>(std::forward<Args>(args)...);
			return std::move(*this);
		}
	};

	struct ImporterDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		EditorImporterPtr        importer;
		ImporterDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		ImporterDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		ImporterDesc&
		AddExtension(std::string extension) &
		{
			extensions.push_back(std::move(extension));
			return *this;
		}

		ImporterDesc&&
		AddExtension(std::string extension) &&
		{
			AddExtension(std::move(extension));
			return std::move(*this);
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorImporter> && std::constructible_from<T, Args...>
		ImporterDesc&
		AddImporter(Args&&... args) &
		{
			importer = std::make_unique<T>(std::forward<Args>(args)...);
			return *this;
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IEditorImporter> && std::constructible_from<T, Args...>
		ImporterDesc&&
		AddImporter(Args&&... args) &&
		{
			AddImporter<T>(std::forward<Args>(args)...);
			return std::move(*this);
		}
	};

	struct ThumbnailProviderDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		ThumbnailProviderPtr     provider;
		ThumbnailProviderDesc&
		SetId(std::string value) &
		{
			id = std::move(value);
			return *this;
		}

		ThumbnailProviderDesc&&
		SetId(std::string value) &&
		{
			SetId(std::move(value));
			return std::move(*this);
		}

		ThumbnailProviderDesc&
		AddExtension(std::string extension) &
		{
			extensions.push_back(std::move(extension));
			return *this;
		}

		ThumbnailProviderDesc&&
		AddExtension(std::string extension) &&
		{
			AddExtension(std::move(extension));
			return std::move(*this);
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IThumbnailProvider> && std::constructible_from<T, Args...>
		ThumbnailProviderDesc&
		AddProvider(Args&&... args) &
		{
			provider = std::make_unique<T>(std::forward<Args>(args)...);
			return *this;
		}

		template <typename T, typename... Args>
			requires std::derived_from<T, IThumbnailProvider> && std::constructible_from<T, Args...>
		ThumbnailProviderDesc&&
		AddProvider(Args&&... args) &&
		{
			AddProvider<T>(std::forward<Args>(args)...);
			return std::move(*this);
		}
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
