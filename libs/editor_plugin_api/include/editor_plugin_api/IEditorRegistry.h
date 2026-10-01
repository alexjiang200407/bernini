#pragma once

#include <editor_plugin_api/IAssetEditorFactory.h>
#include <editor_plugin_api/IEditorAction.h>
#include <editor_plugin_api/IEditorImporter.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IThumbnailProvider.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/TranslationCatalog.h>
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
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetParentId(this Self&& self, std::string value)
		{
			self.parentId = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTitle(this Self&& self, LocalizedText value)
		{
			self.title = std::move(value);
			return std::forward<Self>(self);
		}
	};

	struct PanelDesc
	{
		std::string           id;
		LocalizedText         title;
		EditorPanelFactoryPtr factory;
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTitle(this Self&& self, LocalizedText value)
		{
			self.title = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename T, typename Self, typename... Args>
			requires EditorPanelFactoryFor<T, Args...>
		Self&&
		AddFactory(this Self&& self, Args&&... args)
		{
			self.factory = std::make_unique<T>(std::forward<Args>(args)...);
			return std::forward<Self>(self);
		}
	};

	struct AssetEditorDesc
	{
		std::string              id;
		LocalizedText            title;
		std::vector<std::string> extensions;
		AssetEditorFactoryPtr    factory;
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTitle(this Self&& self, LocalizedText value)
		{
			self.title = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddExtension(this Self&& self, std::string extension)
		{
			self.extensions.push_back(std::move(extension));
			return std::forward<Self>(self);
		}

		template <typename T, typename Self, typename... Args>
			requires AssetEditorFactoryFor<T, Args...>
		Self&&
		AddFactory(this Self&& self, Args&&... args)
		{
			self.factory = std::make_unique<T>(std::forward<Args>(args)...);
			return std::forward<Self>(self);
		}
	};

	struct ActionDesc
	{
		std::string              id;
		LocalizedText            title;
		std::string              menuId;
		std::vector<std::string> extensions;
		EditorActionPtr          action;
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTitle(this Self&& self, LocalizedText value)
		{
			self.title = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMenuId(this Self&& self, std::string value)
		{
			self.menuId = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddExtension(this Self&& self, std::string extension)
		{
			self.extensions.push_back(std::move(extension));
			return std::forward<Self>(self);
		}

		template <typename T, typename Self, typename... Args>
			requires EditorActionFor<T, Args...>
		Self&&
		AddAction(this Self&& self, Args&&... args)
		{
			self.action = std::make_unique<T>(std::forward<Args>(args)...);
			return std::forward<Self>(self);
		}
	};

	struct ImporterDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		EditorImporterPtr        importer;
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddExtension(this Self&& self, std::string extension)
		{
			self.extensions.push_back(std::move(extension));
			return std::forward<Self>(self);
		}

		template <typename T, typename Self, typename... Args>
			requires EditorImporterFor<T, Args...>
		Self&&
		AddImporter(this Self&& self, Args&&... args)
		{
			self.importer = std::make_unique<T>(std::forward<Args>(args)...);
			return std::forward<Self>(self);
		}
	};

	struct ThumbnailProviderDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		ThumbnailProviderPtr     provider;
		template <typename Self>
		Self&&
		SetId(this Self&& self, std::string value)
		{
			self.id = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		AddExtension(this Self&& self, std::string extension)
		{
			self.extensions.push_back(std::move(extension));
			return std::forward<Self>(self);
		}

		template <typename T, typename Self, typename... Args>
			requires ThumbnailProviderFor<T, Args...>
		Self&&
		AddProvider(this Self&& self, Args&&... args)
		{
			self.provider = std::make_unique<T>(std::forward<Args>(args)...);
			return std::forward<Self>(self);
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
