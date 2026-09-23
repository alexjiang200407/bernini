#pragma once

#include <assetlib/AssetStore.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <filesystem>
#include <functional>
#include <gamelib/AssetManager.h>
#include <qwidget.h>
#include <span>
#include <string>
#include <string_view>

class Renderer;
class RenderTargetWindow;

namespace editor::plugins
{
	struct EditorHostDispatch
	{
		std::function<void(std::string_view)>    showPanel;
		std::function<void(std::string_view)>    openAsset;
		std::function<void(std::string_view)>    assetChanged;
		std::function<void(RenderTargetWindow&)> viewportCreated;

		// Dispatched rather than called here: the import owns dialogs and a loading screen, which
		// belong to a window, and this object has none.
		std::function<std::string(const std::filesystem::path&)> importMeshSource;
	};

	class EditorHost final : public IEditorHost
	{
	public:
		EditorHost(
			const assetlib::AssetStore&         store,
			std::span<const TranslationCatalog> catalogs,
			Renderer*                           renderer,
			game::AssetManager*                 assets,
			bool                                headless,
			EditorHostDispatch                  dispatch);

		[[nodiscard]] const assetlib::AssetStore&
		GetStore() const noexcept override;

		[[nodiscard]] const ILanguageResolver&
		GetLanguageResolver() const noexcept override;

		void
		InvokeRender(const RenderWork& work) override;

		[[nodiscard]] IEditorViewport*
		CreateViewport(QWidget* parent, const ViewportDesc& desc) override;

		void
		ShowPanel(std::string_view id) override;

		void
		OpenAsset(std::string_view key) override;

		[[nodiscard]] std::string
		ImportMeshSource(const std::filesystem::path& source) override;

		void
		AssetChanged(std::string_view key) override;

	private:
		const assetlib::AssetStore& m_Store;
		LanguageResolver            m_Language;
		Renderer*                   m_Renderer = nullptr;
		game::AssetManager*         m_Assets   = nullptr;
		bool                        m_Headless = false;
		EditorHostDispatch          m_Dispatch;
	};
}
