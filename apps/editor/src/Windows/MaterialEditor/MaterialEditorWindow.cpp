#include "MaterialEditorWindow.h"

#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeDelegateModelRegistry>

using QtNodes::DataFlowGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::GraphicsView;
using QtNodes::NodeDelegateModelRegistry;

MaterialEditorWindow::MaterialEditorWindow(QWidget* parent, MaterialEditorWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
	auto* splitter = new QSplitter(Qt::Horizontal, this);

	// --- Left: node blackboard ----------------------------------------------------------------
	auto* leftPanel  = new QWidget(splitter);
	auto* leftLayout = new QVBoxLayout(leftPanel);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);

	// Submesh selector: switching submesh swaps the graph. Populated when a mesh is dropped onto the
	// preview; empty and disabled (showing its placeholder) until then.
	auto* submeshSelector = new QComboBox(leftPanel);
	submeshSelector->setPlaceholderText("No submesh");
	submeshSelector->setEnabled(false);
	leftLayout->addWidget(submeshSelector);

	m_Registry   = std::make_shared<NodeDelegateModelRegistry>();
	m_GraphModel = std::make_unique<DataFlowGraphModel>(m_Registry);
	m_GraphScene = new DataFlowGraphicsScene(*m_GraphModel, leftPanel);
	m_GraphView  = new GraphicsView(m_GraphScene);
	leftLayout->addWidget(m_GraphView);

	// --- Right: model preview -----------------------------------------------------------------
	QWidget* rightPanel = nullptr;
	if (m_Desc.gfx)
	{
		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 4;
		sceneDesc.maxMeshlets             = 512;
		sceneDesc.maxSubmeshes            = 8;
		sceneDesc.maxVertexBufferByteSize = 400000;
		sceneDesc.maxIndices              = 20000;
		sceneDesc.maxPbrMaterials         = 8;
		sceneDesc.maxLoosePbrMaterials    = 8;
		m_PreviewScene                    = m_Desc.gfx->CreateScene(sceneDesc);

		auto rtDesc         = RenderTargetWindowDesc();
		rtDesc.gfx          = m_Desc.gfx;
		rtDesc.scene        = m_PreviewScene;
		rtDesc.maxInstances = m_Desc.maxPreviewInstances;

		m_Preview  = new RenderTargetWindow(splitter, std::move(rtDesc));
		rightPanel = m_Preview;
	}
	else
	{
		auto* placeholder = new QLabel("No graphics device", splitter);
		placeholder->setAlignment(Qt::AlignCenter);
		placeholder->setStyleSheet("color: gray;");
		rightPanel = placeholder;
	}

	splitter->addWidget(leftPanel);
	splitter->addWidget(rightPanel);
	// The preview should start with a good share of the width (~38%); a bare native-surface widget
	// reports a tiny size hint, so give the splitter explicit initial sizes. Stretch factors keep the
	// ratio roughly stable on resize.
	splitter->setSizes({ 780, 480 });
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(splitter);
}

MaterialEditorWindow::~MaterialEditorWindow() = default;
