#include "Plugins/EditorHost.h"

#include "Render/Renderer.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/TranslationCatalog.h>

#include <assetlib/AssetStore.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <memory>
#include <qwidget.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace editor::plugins
{
	EditorHost::EditorHost(
		const assetlib::AssetStore&         store,
		std::span<const TranslationCatalog> catalogs,
		Renderer*                           renderer,
		game::AssetManager*                 assets,
		const bool                          headless,
		EditorHostDispatch                  dispatch) :
		m_Store(store), m_Renderer(renderer), m_Assets(assets), m_Headless(headless),
		m_Dispatch(std::move(dispatch))
	{
		for (const TranslationCatalog& catalog : catalogs) m_Language.RegisterCatalog(catalog);
	}

	const assetlib::AssetStore&
	EditorHost::GetStore() const noexcept
	{
		return m_Store;
	}

	const ILanguageResolver&
	EditorHost::GetLanguageResolver() const noexcept
	{
		return m_Language;
	}

	void
	EditorHost::InvokeRender(const RenderWork& work)
	{
		if (m_Renderer == nullptr || m_Assets == nullptr)
			throw std::runtime_error("Editor render services are unavailable");
		m_Renderer->Invoke([&] {
			RenderContext context{ *m_Renderer->GetGraphics(), *m_Renderer->GetScene(), *m_Assets };
			work(context);
		});
	}

	IEditorViewport*
	EditorHost::CreateViewport(QWidget* parent, const ViewportDesc& desc)
	{
		if (parent == nullptr)
			throw std::runtime_error("Editor viewport requires a parent");
		if (m_Renderer == nullptr || m_Assets == nullptr)
			throw std::runtime_error("Editor render services are unavailable");
		auto viewport = std::make_unique<RenderTargetWindow>(
			parent,
			RenderTargetWindowDesc{ .renderer               = m_Renderer,
		                            .assets                 = m_Assets,
		                            .initialInstances       = desc.initialInstances,
		                            .taaEnabled             = desc.taaEnabled,
		                            .renderScale            = desc.renderScale,
		                            .taaReconstructionWidth = desc.taaReconstructionWidth,
		                            .taaSharpness           = desc.taaSharpness,
		                            .bloom                  = { desc.bloomEnabled, desc.bloom },
		                            .colorGrade = { desc.colorGradeEnabled, desc.colorGrade },
		                            .headless   = m_Headless });
		if (m_Dispatch.viewportCreated)
			m_Dispatch.viewportCreated(*viewport);
		return viewport.release();
	}

	void
	EditorHost::ShowPanel(const std::string_view id)
	{
		if (!m_Dispatch.showPanel)
			throw std::runtime_error("Editor panel dispatch is unavailable");
		m_Dispatch.showPanel(id);
	}

	void
	EditorHost::OpenAsset(const std::string_view key)
	{
		if (!m_Dispatch.openAsset)
			throw std::runtime_error("Editor asset dispatch is unavailable");
		m_Dispatch.openAsset(key);
	}

	std::string
	EditorHost::ImportMeshSource(const std::filesystem::path& source)
	{
		if (!m_Dispatch.importMeshSource)
			throw std::runtime_error("Editor import is unavailable");
		return m_Dispatch.importMeshSource(source);
	}

	void
	EditorHost::AssetChanged(const std::string_view key)
	{
		if (!m_Dispatch.assetChanged)
			throw std::runtime_error("Editor asset-change dispatch is unavailable");
		m_Dispatch.assetChanged(key);
	}
}
