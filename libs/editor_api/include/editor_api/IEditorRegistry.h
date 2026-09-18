#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>
#include <editor_api/Thumbnail.h>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	struct PanelDesc
	{
		std::string                                         id;
		QString                                             title;
		std::function<EditorPanel*(IEditorHost&, QWidget*)> create;
	};

	struct AssetEditorDesc
	{
		std::string                                              id;
		QString                                                  title;
		std::vector<std::string>                                 extensions;
		std::function<AssetEditorPanel*(IEditorHost&, QWidget*)> create;
	};

	struct ActionDesc
	{
		std::string                                                     id;
		QString                                                         title;
		QStringList                                                     menu;
		std::vector<std::string>                                        extensions;
		std::function<bool(IEditorHost&, std::span<const std::string>)> enabled;
		std::function<void(IEditorHost&, std::span<const std::string>)> invoke;
	};

	struct ImporterDesc
	{
		std::string              id;
		std::vector<std::string> extensions;
		std::function<void(IEditorHost&, const std::filesystem::path&, std::string_view)> import;
	};

	struct ThumbnailProviderDesc
	{
		std::string                                                             id;
		std::vector<std::string>                                                extensions;
		std::function<Thumbnail(const assetlib::AssetStore&, std::string_view)> describe;
	};

	/** Startup only. Own descriptors by value; reject invalid IDs/callbacks and collisions. See docs/editor_plugins.md. */
	class IEditorRegistry
	{
	public:
		virtual ~IEditorRegistry() = default;

		/** Factories run lazily on the GUI thread; return a non-null child of the supplied parent. */
		virtual void
		AddPanel(PanelDesc desc) = 0;

		/** One opener per extension. Editor IDs share the panel ID namespace. */
		virtual void
		AddAssetEditor(AssetEditorDesc desc) = 0;

		/** Empty extensions means a menu action; otherwise every selected asset must match. */
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
