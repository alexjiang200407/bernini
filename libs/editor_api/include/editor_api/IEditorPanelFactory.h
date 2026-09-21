#pragma once
#include <QWidget>
#include <editor_api/EditorPanel.h>
#include <editor_api/IEditorHost.h>
#include <memory>

namespace editor
{
	// Registry-owned factory; only the returned project panel may retain its borrowed host.
	class IEditorPanelFactory
	{
	public:
		virtual ~IEditorPanelFactory()                  = default;
		IEditorPanelFactory(const IEditorPanelFactory&) = delete;
		IEditorPanelFactory(IEditorPanelFactory&&)      = delete;
		IEditorPanelFactory&
		operator=(const IEditorPanelFactory&) = delete;
		IEditorPanelFactory&
		operator=(IEditorPanelFactory&&) = delete;

		virtual EditorPanel*
		Create(IEditorHost& host, QWidget* parent) = 0;

	protected:
		IEditorPanelFactory() = default;
	};
	using EditorPanelFactoryPtr = std::unique_ptr<IEditorPanelFactory>;

}
