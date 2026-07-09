#pragma once

#include <QWidget>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

#include "Windows/MaterialEditor/MaterialPreviewWindow.h"

class QComboBox;

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
	// Rebuilds the submesh selector + one graph per submesh from the preview geometry.
	void
	SetPreviewGeometry(const QStringList& submeshNames);

	// Shows the selected submesh's graph (each submesh has its own; switching clears the board).
	void
	SelectSubmesh(int index);

	MaterialEditorWindowDesc m_Desc;

	bgl::SceneHandle       m_PreviewScene;
	MaterialPreviewWindow* m_Preview = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;

	// One node graph per submesh of the current preview geometry. `scene` references `model`, so it
	// is declared after it (destroyed first).
	struct SubmeshGraph
	{
		std::unique_ptr<QtNodes::DataFlowGraphModel>    model;
		std::unique_ptr<QtNodes::DataFlowGraphicsScene> scene;
	};
	std::vector<SubmeshGraph> m_SubmeshGraphs;
	int                       m_CurrentSubmesh = -1;

	QComboBox*             m_SubmeshSelector = nullptr;
	QtNodes::GraphicsView* m_GraphView       = nullptr;
};
