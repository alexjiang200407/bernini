#pragma once

#include <QWidget>
#include <assetlib/AssetStore.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <filesystem>
#include <string>
#include <string_view>

namespace editor
{
	/** One open project's services; destroy panels and drain their work before replacing this object. */
	class IEditorHost
	{
	public:
		virtual ~IEditorHost() = default;

		[[nodiscard]] virtual const assetlib::AssetStore&
		GetStore() const noexcept = 0;

		/** Host-owned and valid for this host lifetime; plugins cannot change its locale or catalogs. */
		[[nodiscard]] virtual const ILanguageResolver&
		GetLanguageResolver() const noexcept = 0;

		/** Synchronous; exceptions return to the caller. No GUI waits or borrowed references escaping work. */
		virtual void
		InvokeRender(const RenderWork& work) = 0;

		/** GUI thread; non-null parent owns the returned widget. The host selects native/headless output. */
		[[nodiscard]] virtual IEditorViewport*
		CreateViewport(QWidget* parent, const ViewportDesc& desc) = 0;

		virtual void
		ShowPanel(std::string_view id) = 0;

		virtual void
		OpenAsset(std::string_view key) = 0;

		/**
		 * Imports a mesh source -- a glTF, from anywhere on disk -- into the open project, and
		 * answers with the key of the `.bmesh` it wrote.
		 *
		 * For a panel handed a source the project has never imported. Resolving one that *is*
		 * imported needs no host: the document beside it names its outputs.
		 *
		 * GUI thread, and it blocks. The host asks the user for the import's options and runs the
		 * cook behind a screen of its own, so this returns only once the import has finished, been
		 * declined or been refused -- and parents that UI itself, since a plugin panel does not
		 * decide what the host's dialogs are modal to.
		 *
		 * `source` is a filesystem path and the answer a mount key because that is what each one
		 * is: a file the user picked from anywhere, and a file in the project (STYLE.md § Paths).
		 *
		 * @return Empty when the user declined or cancelled, when the import failed, and when it
		 *         wrote no mesh at all -- a source imported for its clips alone. The host has
		 *         already said whatever there was to say, so an empty answer is not the caller's
		 *         to report.
		 */
		[[nodiscard]] virtual std::string
		ImportMeshSource(const std::filesystem::path& source) = 0;

		/** GUI thread after a successful write; invalidate caches and queue notifications to live panels. */
		virtual void
		AssetChanged(std::string_view key) = 0;
	};
}
