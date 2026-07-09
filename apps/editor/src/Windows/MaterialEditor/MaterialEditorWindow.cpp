#include "MaterialEditorWindow.h"

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

	// --- Left: submesh selector + node blackboard ---------------------------------------------
	auto* leftPanel  = new QWidget(splitter);
	auto* leftLayout = new QVBoxLayout(leftPanel);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);

	m_SubmeshSelector = new QComboBox(leftPanel);
	m_SubmeshSelector->setPlaceholderText("No submesh");
	m_SubmeshSelector->setEnabled(false);
	connect(
		m_SubmeshSelector,
		&QComboBox::currentIndexChanged,
		this,
		&MaterialEditorWindow::SelectSubmesh);
	leftLayout->addWidget(m_SubmeshSelector);

	m_Registry = std::make_shared<NodeDelegateModelRegistry>();
	// (node types are registered in a later increment)

	m_GraphView = new GraphicsView(leftPanel);  // its scene is set per selected submesh
	leftLayout->addWidget(m_GraphView);

	// --- Right: model preview -----------------------------------------------------------------
	QWidget* rightPanel = nullptr;
	if (m_Desc.gfx)
	{
		// Budgeted for a dropped mesh, not just the default sphere.
		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 8;
		sceneDesc.maxMeshlets             = 8192;
		sceneDesc.maxSubmeshes            = 128;
		sceneDesc.maxVertexBufferByteSize = 8000000;
		sceneDesc.maxIndices              = 400000;
		sceneDesc.maxPbrMaterials         = 16;
		sceneDesc.maxLoosePbrMaterials    = 16;
		m_PreviewScene                    = m_Desc.gfx->CreateScene(sceneDesc);

		auto rtDesc         = RenderTargetWindowDesc();
		rtDesc.gfx          = m_Desc.gfx;
		rtDesc.scene        = m_PreviewScene;
		rtDesc.maxInstances = m_Desc.maxPreviewInstances;

		m_Preview  = new MaterialPreviewWindow(splitter, std::move(rtDesc), m_Desc.previewEnv);
		rightPanel = m_Preview;

		// Dropping a mesh onto the preview swaps its geometry; rebuild the submesh selector.
		connect(m_Preview, &MaterialPreviewWindow::GeometryChanged, this, [this]() {
			SetPreviewGeometry(m_Preview->SubmeshNames());
		});
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

	// Populate the selector from the preview geometry (the default sphere for now). Each submesh has
	// its own graph. The sphere has no backing material file, so its graph is in-memory only and
	// cannot be saved -- a material asset opened from disk would (a later increment).
	if (m_Preview)
		SetPreviewGeometry(m_Preview->SubmeshNames());
}

MaterialEditorWindow::~MaterialEditorWindow()
{
	// Detach the view before the per-submesh scenes/models are destroyed, so the view never holds a
	// dangling scene pointer during teardown.
	if (m_GraphView)
		m_GraphView->setScene(nullptr);
}

void
MaterialEditorWindow::SetPreviewGeometry(const QStringList& submeshNames)
{
	m_SubmeshSelector->clear();
	m_SubmeshGraphs.clear();
	m_CurrentSubmesh = -1;

	for (const QString& name : submeshNames)
	{
		SubmeshGraph graph;
		graph.model = std::make_unique<DataFlowGraphModel>(m_Registry);
		graph.scene = std::make_unique<DataFlowGraphicsScene>(*graph.model);
		m_SubmeshGraphs.push_back(std::move(graph));
		m_SubmeshSelector->addItem(name);
	}

	m_SubmeshSelector->setEnabled(!m_SubmeshGraphs.empty());
	if (!m_SubmeshGraphs.empty())
		m_SubmeshSelector->setCurrentIndex(0);
}

void
MaterialEditorWindow::SelectSubmesh(int index)
{
	if (index < 0 || index >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	// Switching submesh swaps the blackboard to that submesh's own graph.
	m_CurrentSubmesh = index;
	m_GraphView->setScene(m_SubmeshGraphs[static_cast<size_t>(index)].scene.get());
}
