#pragma once

#include <QWidget>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

#include "Windows/MaterialEditor/MaterialPreviewWindow.h"

namespace QtNodes
{
	class DataFlowGraphModel;
	class DataFlowGraphicsScene;
	class GraphicsView;
	class NodeDelegateModelRegistry;
}

struct MaterialEditorWindowDesc
{
	bgl::GraphicsHandle gfx                 = nullptr;
	uint32_t            maxPreviewInstances = 16;
	MaterialPreviewEnv  previewEnv;  // skybox + IBL paths from config (materialEditor.*)
};

// The material-authoring surface: a node blackboard (QtNodes) on the left and a live model preview
// on the right. The preview owns its own scene so its geometry never leaks into the level view.
// The node model and preview content are filled in later; this is the window shell + wiring.
class MaterialEditorWindow : public QWidget
{
	Q_OBJECT

public:
	explicit MaterialEditorWindow(QWidget* parent = nullptr, MaterialEditorWindowDesc desc = {});
	~MaterialEditorWindow() override;

private:
	MaterialEditorWindowDesc m_Desc;

	bgl::SceneHandle       m_PreviewScene;
	MaterialPreviewWindow* m_Preview = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;
	std::unique_ptr<QtNodes::DataFlowGraphModel>        m_GraphModel;
	QtNodes::DataFlowGraphicsScene*                     m_GraphScene = nullptr;
	QtNodes::GraphicsView*                              m_GraphView  = nullptr;
};
