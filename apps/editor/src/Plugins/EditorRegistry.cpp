#include "Plugins/EditorRegistry.h"
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/LocalizedText.h>
#include <editor_plugin_api/TranslationCatalog.h>

#include <algorithm>
#include <cstddef>
#include <editor_plugin_api/LanguageResolver.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::plugins
{
	namespace
	{
		bool
		IsId(const std::string_view id)
		{
			bool componentStart = true;
			bool sawDot         = false;
			for (const char c : id)
			{
				if (componentStart)
				{
					if (c < 'a' || c > 'z')
						return false;
					componentStart = false;
					continue;
				}
				if (c == '.')
				{
					componentStart = true;
					sawDot         = true;
					continue;
				}
				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
					return false;
			}
			return sawDot && !componentStart;
		}

		bool
		IsExtension(const std::string_view extension)
		{
			return extension.size() > 1 && extension.front() == '.' &&
			       std::ranges::all_of(extension.substr(1), [](const char c) {
					   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
				   });
		}

		void
		RequireId(const std::string_view id)
		{
			if (!IsId(id))
				throw std::runtime_error("Invalid editor contribution ID");
			if (id.starts_with("editor."))
				throw std::runtime_error("Editor contribution ID is reserved");
		}

		template <typename Desc>
		void
		RequireUnique(const std::string_view id, const std::vector<Desc>& values)
		{
			RequireId(id);
			if (std::ranges::any_of(values, [&](const Desc& value) { return value.id == id; }))
				throw std::runtime_error("Editor contribution ID collides");
		}

		void
		RequireText(const LocalizedText& text)
		{
			LanguageResolver validation;
			validation.RegisterCatalog({ text.context, { { text.key, "en", text.fallback } } });
		}

		void
		RequireExtensions(const std::vector<std::string>& extensions)
		{
			if (extensions.empty())
				throw std::runtime_error("Editor contribution requires an extension");
			for (const std::string& extension : extensions)
				if (!IsExtension(extension))
					throw std::runtime_error("Invalid editor contribution extension");
			for (std::size_t i = 0; i < extensions.size(); ++i)
				if (std::find(
						extensions.begin() + static_cast<std::ptrdiff_t>(i + 1),
						extensions.end(),
						extensions[i]) != extensions.end())
					throw std::runtime_error("Duplicate editor contribution extension");
		}

		template <typename Desc>
		const Desc*
		FindByExtension(const std::vector<Desc>& values, const std::string_view extension)
		{
			const auto found = std::ranges::find_if(values, [&](const Desc& value) {
				return std::ranges::find(value.extensions, extension) != value.extensions.end();
			});
			return found == values.end() ? nullptr : &*found;
		}
	}

	void
	EditorRegistry::Register(IEditorPlugin& plugin, std::vector<TranslationCatalog> catalogs)
	{
		const auto translations = m_Catalogs.size();
		const auto menus        = m_Menus.size();
		const auto panels       = m_Panels.size();
		const auto editors      = m_AssetEditors.size();
		const auto actions      = m_Actions.size();
		const auto importers    = m_Importers.size();
		const auto thumbnails   = m_ThumbnailProviders.size();
		try
		{
			plugin.Register(*this);
			for (TranslationCatalog& catalog : catalogs) AddTranslations(std::move(catalog));
		}
		catch (...)
		{
			m_ThumbnailProviders.resize(thumbnails);
			m_Importers.resize(importers);
			m_Actions.resize(actions);
			m_AssetEditors.resize(editors);
			m_Panels.resize(panels);
			m_Menus.resize(menus);
			m_Catalogs.resize(translations);
			throw;
		}
	}

	void
	EditorRegistry::AddTranslations(TranslationCatalog catalog)
	{
		LanguageResolver validation;
		validation.RegisterCatalog(catalog);
		if (catalog.context.starts_with("editor."))
			throw std::runtime_error("Translation context is reserved");
		if (std::ranges::any_of(m_Catalogs, [&](const TranslationCatalog& existing) {
				return existing.context == catalog.context;
			}))
			throw std::runtime_error("Translation context already registered");
		m_Catalogs.push_back(std::move(catalog));
	}

	void
	EditorRegistry::AddMenu(MenuDesc desc)
	{
		RequireUnique(desc.id, m_Menus);
		RequireText(desc.title);
		if (!desc.parentId.empty() && desc.parentId != c_FileMenuId &&
		    desc.parentId != c_ToolsMenuId &&
		    std::ranges::none_of(m_Menus, [&](const MenuDesc& menu) {
				return menu.id == desc.parentId;
			}))
			throw std::runtime_error("Editor menu parent is not registered");
		m_Menus.push_back(std::move(desc));
	}

	void
	EditorRegistry::AddPanel(PanelDesc desc)
	{
		RequireUnique(desc.id, m_Panels);
		RequireText(desc.title);
		if (std::ranges::any_of(m_AssetEditors, [&](const AssetEditorDesc& value) {
				return value.id == desc.id;
			}))
			throw std::runtime_error("Editor panel ID collides");
		if (!desc.factory)
			throw std::runtime_error("Editor panel factory is missing");
		m_Panels.push_back(std::move(desc));
	}

	void
	EditorRegistry::AddAssetEditor(AssetEditorDesc desc)
	{
		RequireUnique(desc.id, m_AssetEditors);
		RequireText(desc.title);
		if (std::ranges::any_of(m_Panels, [&](const PanelDesc& value) {
				return value.id == desc.id;
			}))
			throw std::runtime_error("Editor panel ID collides");
		if (!desc.factory)
			throw std::runtime_error("Asset editor factory is missing");
		RequireExtensions(desc.extensions);
		for (const std::string& extension : desc.extensions)
			if (FindAssetEditor(extension) != nullptr)
				throw std::runtime_error("Asset editor extension collides");
		m_AssetEditors.push_back(std::move(desc));
	}

	void
	EditorRegistry::AddAction(ActionDesc desc)
	{
		RequireUnique(desc.id, m_Actions);
		RequireText(desc.title);
		if (!desc.action)
			throw std::runtime_error("Editor action is missing");
		if (desc.extensions.empty())
		{
			if (desc.menuId != c_FileMenuId && desc.menuId != c_ToolsMenuId &&
			    std::ranges::none_of(m_Menus, [&](const MenuDesc& menu) {
					return menu.id == desc.menuId;
				}))
				throw std::runtime_error("Editor action menu is not registered");
		}
		else
		{
			if (!desc.menuId.empty())
				throw std::runtime_error("Content action cannot name a menu");
			RequireExtensions(desc.extensions);
		}
		m_Actions.push_back(std::move(desc));
	}

	void
	EditorRegistry::AddImporter(ImporterDesc desc)
	{
		RequireUnique(desc.id, m_Importers);
		if (!desc.importer)
			throw std::runtime_error("Editor importer is missing");
		RequireExtensions(desc.extensions);
		for (const std::string& extension : desc.extensions)
		{
			if (extension == ".glb" || extension == ".gltf" || extension == ".hdr")
				throw std::runtime_error("Editor importer extension collides");
			if (FindImporter(extension) != nullptr)
				throw std::runtime_error("Editor importer extension collides");
		}
		m_Importers.push_back(std::move(desc));
	}

	void
	EditorRegistry::AddThumbnailProvider(ThumbnailProviderDesc desc)
	{
		RequireUnique(desc.id, m_ThumbnailProviders);
		if (!desc.provider)
			throw std::runtime_error("Thumbnail provider callback is missing");
		RequireExtensions(desc.extensions);
		for (const std::string& extension : desc.extensions)
		{
			if (extension == ".bmesh" || extension == ".bmaterial")
				throw std::runtime_error("Thumbnail provider extension collides");
			if (FindThumbnailProvider(extension) != nullptr)
				throw std::runtime_error("Thumbnail provider extension collides");
		}
		m_ThumbnailProviders.push_back(std::move(desc));
	}

	std::span<const TranslationCatalog>
	EditorRegistry::Catalogs() const noexcept
	{
		return m_Catalogs;
	}
	std::span<const MenuDesc>
	EditorRegistry::Menus() const noexcept
	{
		return m_Menus;
	}
	std::span<const PanelDesc>
	EditorRegistry::Panels() const noexcept
	{
		return m_Panels;
	}
	std::span<const AssetEditorDesc>
	EditorRegistry::AssetEditors() const noexcept
	{
		return m_AssetEditors;
	}
	std::span<const ActionDesc>
	EditorRegistry::Actions() const noexcept
	{
		return m_Actions;
	}
	std::span<const ImporterDesc>
	EditorRegistry::Importers() const noexcept
	{
		return m_Importers;
	}
	std::span<const ThumbnailProviderDesc>
	EditorRegistry::ThumbnailProviders() const noexcept
	{
		return m_ThumbnailProviders;
	}

	const PanelDesc*
	EditorRegistry::FindPanel(const std::string_view id) const noexcept
	{
		const auto found = std::ranges::find(m_Panels, id, &PanelDesc::id);
		return found == m_Panels.end() ? nullptr : &*found;
	}

	const AssetEditorDesc*
	EditorRegistry::FindAssetEditor(const std::string_view extension) const noexcept
	{
		return FindByExtension(m_AssetEditors, extension);
	}
	const ImporterDesc*
	EditorRegistry::FindImporter(const std::string_view extension) const noexcept
	{
		return FindByExtension(m_Importers, extension);
	}
	const ThumbnailProviderDesc*
	EditorRegistry::FindThumbnailProvider(const std::string_view extension) const noexcept
	{
		return FindByExtension(m_ThumbnailProviders, extension);
	}
}
