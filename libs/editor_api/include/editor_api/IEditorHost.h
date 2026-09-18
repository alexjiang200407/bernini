#pragma once

#include <QWidget>
#include <editor_api/IEditorViewport.h>
#include <functional>
#include <string_view>

namespace assetlib
{
	class AssetStore;
}

namespace editor
{
	/** One open project's services; destroy panels and drain their work before replacing this object. */
	class IEditorHost
	{
	public:
		virtual ~IEditorHost() = default;

		[[nodiscard]] virtual const assetlib::AssetStore&
		GetStore() const noexcept = 0;

		/** Synchronous; exceptions return to the caller. No GUI waits or borrowed references escaping work. */
		virtual void
		InvokeRender(const std::function<void(RenderContext&)>& work) = 0;

		/** GUI thread; non-null parent owns the returned widget. The host selects native/headless output. */
		[[nodiscard]] virtual IEditorViewport*
		CreateViewport(QWidget* parent, const ViewportDesc& desc) = 0;

		virtual void
		ShowPanel(std::string_view id) = 0;

		virtual void
		OpenAsset(std::string_view key) = 0;

		/** After a successful store write, invalidate dependent previews and explorer metadata. */
		virtual void
		AssetChanged(std::string_view key) = 0;
	};
}
