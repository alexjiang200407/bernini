#pragma once
#include <QWidget>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>

namespace editor
{
	// Registry-owned factory; only the returned project panel may retain its borrowed host.
	class IAssetEditorFactory
	{
	public:
		virtual ~IAssetEditorFactory()                  = default;
		IAssetEditorFactory(const IAssetEditorFactory&) = delete;
		IAssetEditorFactory(IAssetEditorFactory&&)      = delete;
		IAssetEditorFactory&
		operator=(const IAssetEditorFactory&) = delete;
		IAssetEditorFactory&
		operator=(IAssetEditorFactory&&) = delete;

		virtual AssetEditorPanel*
		Create(IEditorHost& host, QWidget* parent) = 0;

	protected:
		IAssetEditorFactory() = default;
	};
}
