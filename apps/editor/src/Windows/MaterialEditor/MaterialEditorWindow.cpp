#include "MaterialEditorWindow.h"

#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/NodeDelegateModelRegistry>

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>

#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

using QtNodes::DataFlowGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::NodeDelegateModelRegistry;

namespace
{
	// The graph's single sink. Every material is compiled from it.
	MaterialOutputNode*
	FindOutputNode(DataFlowGraphModel& model)
	{
		for (const QtNodes::NodeId nodeId : model.allNodeIds())
		{
			if (auto* output = model.delegateModel<MaterialOutputNode>(nodeId))
				return output;
		}
		return nullptr;
	}

	// A `.bmaterial`'s texture references are relative to the material file, so the asset stays
	// relocatable. Texture nodes hold absolute paths while the graph is live, so the saved graph is
	// rewritten on the way out and back in. Paths that cannot be expressed relative to `dir` (a
	// different drive, say) are left absolute -- still correct, merely not relocatable.
	QString
	Rebase(const QString& path, const std::filesystem::path& dir, bool toRelative)
	{
		if (path.isEmpty())
			return path;

		std::error_code       ec;
		const auto            source = std::filesystem::path(path.toStdWString());
		std::filesystem::path result =
			toRelative ? std::filesystem::relative(source, dir, ec) : (dir / source);
		if (ec || result.empty())
			return path;

		result = toRelative ? result : std::filesystem::weakly_canonical(result, ec);
		if (ec)
			return path;

		return QString::fromStdWString(result.generic_wstring());
	}

	// Rewrites every Texture node's stored path in a saved graph. QtNodes nests each delegate's own
	// save() under "internal-data", which is where TextureNode wrote its "texture" key.
	void
	RebaseGraphTextures(QJsonObject& graph, const std::filesystem::path& dir, bool toRelative)
	{
		QJsonArray nodes = graph["nodes"].toArray();
		for (QJsonValueRef nodeValue : nodes)
		{
			QJsonObject node     = nodeValue.toObject();
			QJsonObject internal = node["internal-data"].toObject();

			if (internal["model-name"].toString() != QLatin1String("Texture"))
				continue;

			internal["texture"]   = Rebase(internal["texture"].toString(), dir, toRelative);
			node["internal-data"] = internal;
			nodeValue             = node;
		}
		graph["nodes"] = nodes;
	}
}

MaterialEditorWindow::MaterialEditorWindow(QWidget* parent, MaterialEditorWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
	auto* splitter = new QSplitter(Qt::Horizontal, this);

	// --- Left: submesh selector + node blackboard ---------------------------------------------
	auto* leftPanel  = new QWidget(splitter);
	auto* leftLayout = new QVBoxLayout(leftPanel);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->setSpacing(0);

	// Material asset actions, acting on the selected submesh's graph.
	auto* toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(2, 2, 2, 2);

	m_OpenButton   = new QPushButton(QStringLiteral("Open..."), leftPanel);
	m_SaveButton   = new QPushButton(QStringLiteral("Save"), leftPanel);
	m_SaveAsButton = new QPushButton(QStringLiteral("Save As..."), leftPanel);

	connect(m_OpenButton, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			window(),
			QStringLiteral("Open Material"),
			QString(),
			QStringLiteral("Bernini Material (*.bmaterial)"));
		if (!path.isEmpty())
			OpenMaterialInto(m_CurrentSubmesh, path);
	});
	connect(m_SaveButton, &QPushButton::clicked, this, [this]() { SaveCurrentMaterial(false); });
	connect(m_SaveAsButton, &QPushButton::clicked, this, [this]() { SaveCurrentMaterial(true); });

	// Names the `.bmaterial` the selected submesh is bound to, so it is clear what Save writes to.
	m_MaterialLabel = new QLabel(leftPanel);
	m_MaterialLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_MaterialLabel->setStyleSheet("color: gray;");

	toolbar->addWidget(m_OpenButton);
	toolbar->addWidget(m_SaveButton);
	toolbar->addWidget(m_SaveAsButton);
	toolbar->addWidget(m_MaterialLabel, 1);
	leftLayout->addLayout(toolbar);

	m_SubmeshSelector = new QComboBox(leftPanel);
	m_SubmeshSelector->setPlaceholderText("No submesh");
	m_SubmeshSelector->setEnabled(false);
	connect(
		m_SubmeshSelector,
		&QComboBox::currentIndexChanged,
		this,
		&MaterialEditorWindow::SelectSubmesh);
	leftLayout->addWidget(m_SubmeshSelector);

	m_GraphView = new MaterialGraphView(leftPanel);  // its scene is set per selected submesh
	leftLayout->addWidget(m_GraphView);
	connect(
		m_GraphView,
		&MaterialGraphView::TextureDropped,
		this,
		&MaterialEditorWindow::AddTextureNode);

	// --- Right: model preview -----------------------------------------------------------------
	QWidget* rightPanel = nullptr;
	if (m_Desc.gfx)
	{
		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 16;
		sceneDesc.maxMeshlets             = 8192;
		sceneDesc.maxSubmeshes            = 128;
		sceneDesc.maxVertexBufferByteSize = 8000000;
		sceneDesc.maxIndices              = 400000;
		sceneDesc.maxPbrMaterials         = 16;
		sceneDesc.maxLoosePbrMaterials    = 256;
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

	m_Registry         = std::make_shared<NodeDelegateModelRegistry>();
	bgl::IScene* scene = m_PreviewScene ? m_PreviewScene.Get() : nullptr;
	m_Registry->registerModel<TextureNode>(
		[scene]() { return std::make_unique<TextureNode>(scene); },
		"Input");
	m_Registry->registerModel<MaterialOutputNode>(
		[]() { return std::make_unique<MaterialOutputNode>(); },
		"Output");

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
	// its own graph, and the graph is bound to a `.bmaterial` once saved or opened. The sphere is not
	// backed by any asset, so it starts unbound: Save As gives it a file, Save alone cannot.
	if (m_Preview)
		SetPreviewGeometry(m_Preview->SubmeshNames());
	else
		RefreshActions();  // no preview scene, so no graphs: everything stays disabled
}

MaterialEditorWindow::~MaterialEditorWindow()
{
	// Detach the view before the per-submesh scenes/models are destroyed, so the view never holds a
	// dangling scene pointer during teardown.
	if (m_GraphView)
		m_GraphView->setScene(nullptr);
}

MaterialOutputNode*
MaterialEditorWindow::ResetGraph(int submeshIndex, const QJsonObject& graph)
{
	SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)];

	// The view must not hold a scene whose model is about to die, and QtNodes requires an empty model
	// before load() -- restored nodes keep their saved ids and would clash. So rebuild both.
	if (m_CurrentSubmesh == submeshIndex)
		m_GraphView->setScene(nullptr);

	entry.scene.reset();
	entry.model = std::make_unique<DataFlowGraphModel>(m_Registry);
	entry.scene = std::make_unique<DataFlowGraphicsScene>(*entry.model);

	if (graph.isEmpty())
	{
		// A fresh graph holds just the sink node the material is compiled from.
		const QtNodes::NodeId outputId = entry.model->addNode(QStringLiteral("MaterialOutput"));
		entry.model->setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(220.0, 40.0));
	}
	else
	{
		entry.model->load(graph);
	}

	if (m_CurrentSubmesh == submeshIndex)
		m_GraphView->setScene(entry.scene.get());

	// Recompile whenever anything the material depends on changes. The Output node is the only sink,
	// and every upstream edit reaches it through setInData.
	MaterialOutputNode* output = FindOutputNode(*entry.model);
	if (output != nullptr)
	{
		connect(output, &MaterialOutputNode::Changed, this, [this, submeshIndex]() {
			CompileGraph(submeshIndex);
		});
	}
	return output;
}

void
MaterialEditorWindow::SetPreviewGeometry(const QStringList& submeshNames)
{
	m_SubmeshSelector->clear();
	m_GraphView->setScene(nullptr);
	m_SubmeshGraphs.clear();
	m_CurrentSubmesh = -1;

	const QStringList materialPaths =
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths() : QStringList();

	m_SubmeshGraphs.resize(static_cast<size_t>(submeshNames.size()));
	for (int index = 0; index < submeshNames.size(); ++index)
	{
		ResetGraph(index, QJsonObject());
		m_SubmeshSelector->addItem(submeshNames[index]);

		const QString materialPath = materialPaths.value(index);
		if (!materialPath.isEmpty() &&
		    std::filesystem::exists(std::filesystem::path(materialPath.toStdWString())))
		{
			OpenMaterialInto(index, materialPath, false);
		}
	}

	m_SubmeshSelector->setEnabled(!m_SubmeshGraphs.empty());
	if (!m_SubmeshGraphs.empty())
		m_SubmeshSelector->setCurrentIndex(0);

	RefreshActions();
}

void
MaterialEditorWindow::SelectSubmesh(int index)
{
	if (index < 0 || index >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	// Switching submesh swaps the blackboard to that submesh's own graph.
	m_CurrentSubmesh = index;
	m_GraphView->setScene(m_SubmeshGraphs[static_cast<size_t>(index)].scene.get());
	RefreshActions();
}

void
MaterialEditorWindow::RefreshActions()
{
	const bool hasGraph =
		m_CurrentSubmesh >= 0 && m_CurrentSubmesh < static_cast<int>(m_SubmeshGraphs.size());

	m_OpenButton->setEnabled(hasGraph);
	m_SaveAsButton->setEnabled(hasGraph);

	const QString materialPath =
		hasGraph ? m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)].materialPath : QString();

	// "Save" needs somewhere to save to. The default sphere has no backing asset, so it stays
	// disabled there until the graph has been given a path by Save As.
	m_SaveButton->setEnabled(!materialPath.isEmpty());

	// Show the file name, with the full path on hover -- the toolbar is too narrow for a whole path.
	m_MaterialLabel->setText(
		materialPath.isEmpty() ? QStringLiteral("(unsaved)") : QFileInfo(materialPath).fileName());
	m_MaterialLabel->setToolTip(materialPath);
}

void
MaterialEditorWindow::AddTextureNode(const QString& path, const QPointF& scenePos)
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	DataFlowGraphModel& model = *m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)].model;

	const QtNodes::NodeId nodeId = model.addNode(QStringLiteral("Texture"));
	model.setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);

	if (auto* texture = model.delegateModel<TextureNode>(nodeId))
		texture->SetTexturePath(path);
}

assetlib::BMaterial
MaterialEditorWindow::BuildMaterial(int submeshIndex, const QString& materialPath) const
{
	const SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)];
	const auto          dir   = std::filesystem::path(materialPath.toStdWString()).parent_path();

	auto material = assetlib::BMaterial();

	material.mode = assetlib::MaterialMode::kLoose;
	material.name = QFileInfo(materialPath).completeBaseName().toStdString();

	MaterialOutputNode* output = FindOutputNode(*entry.model);
	if (output != nullptr)
	{
		material.baseColorFactor = output->BaseColorFactor();
		material.metallicFactor  = output->MetallicFactor();
		material.roughnessFactor = output->RoughnessFactor();

		for (unsigned int i = 0; i < assetlib::c_LooseChannelCount; ++i)
		{
			const ChannelData::Route wired = output->Route(i);

			material.routes[i].texture = Rebase(wired.path, dir, true).toStdString();
			material.routes[i].channel = wired.channel;
		}
	}

	// Keep the graph itself alongside the routes it compiles to, so reopening restores the authoring
	// state -- node positions, factors, and nodes that are not wired to anything.
	QJsonObject graph = entry.model->save();
	RebaseGraphTextures(graph, dir, true);
	material.editorGraph = QJsonDocument(graph).toJson(QJsonDocument::Compact).toStdString();

	return material;
}

void
MaterialEditorWindow::SaveCurrentMaterial(bool saveAs)
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)];

	QString path = entry.materialPath;
	if (saveAs || path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(
			window(),
			QStringLiteral("Save Material"),
			path.isEmpty() ? m_SubmeshSelector->currentText() : path,
			QStringLiteral("Bernini Material (*.bmaterial)"));
		if (path.isEmpty())
			return;  // cancelled

		if (QFileInfo(path).suffix().isEmpty())
			path += QStringLiteral(".bmaterial");
	}

	try
	{
		assetlib::saveMaterial(
			BuildMaterial(m_CurrentSubmesh, path),
			std::filesystem::path(path.toStdWString()));
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: failed to save '%s': %s", qPrintable(path), e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Save Material"),
			QStringLiteral("Could not save the material:\n%1").arg(QString::fromLatin1(e.what())));
		return;
	}

	entry.materialPath = path;
	AttachMaterialToMesh(m_CurrentSubmesh, path);
	RefreshActions();
}

void
MaterialEditorWindow::AttachMaterialToMesh(int submeshIndex, const QString& materialPath)
{
	if (m_Preview == nullptr)
		return;

	const std::filesystem::path meshPath = m_Preview->MeshPath();
	if (meshPath.empty())
		return;

	const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submeshIndex));
	if (source == assetlib::c_InvalidIndex)
		return;

	try
	{
		auto mesh = assetlib::load(meshPath);

		const std::string relative =
			Rebase(materialPath, meshPath.parent_path(), true).toStdString();

		if (assetlib::attachMaterial(mesh, source, relative))
			assetlib::save(mesh, meshPath);
	}
	catch (const std::exception& e)
	{
		qWarning(
			"MaterialEditor: saved the material but could not attach it to '%s': %s",
			meshPath.string().c_str(),
			e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Save Material"),
			QStringLiteral(
				"The material was saved, but the mesh could not be updated to "
				"reference it:\n%1")
				.arg(QString::fromLatin1(e.what())));
	}
}

void
MaterialEditorWindow::OpenMaterialInto(int submeshIndex, const QString& path, bool interactive)
{
	if (submeshIndex < 0 || submeshIndex >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	auto material = assetlib::BMaterial();
	try
	{
		material = assetlib::loadMaterial(std::filesystem::path(path.toStdWString()));
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: failed to open '%s': %s", qPrintable(path), e.what());
		if (interactive)
		{
			QMessageBox::warning(
				window(),
				QStringLiteral("Open Material"),
				QStringLiteral("Could not open the material:\n%1")
					.arg(QString::fromLatin1(e.what())));
		}
		return;
	}

	const auto dir = std::filesystem::path(path.toStdWString()).parent_path();

	// The stored graph is authoritative for the editor: it reproduces the exact board that produced
	// these routes. A material with no graph was not authored here (imported from glTF, or exported
	// and stripped), so its texture references are rebuilt below instead.
	auto graph = QJsonObject();
	if (!material.editorGraph.empty())
	{
		QJsonParseError     error{};
		const QJsonDocument doc =
			QJsonDocument::fromJson(QByteArray::fromStdString(material.editorGraph), &error);
		if (error.error == QJsonParseError::NoError && doc.isObject())
		{
			graph = doc.object();
			RebaseGraphTextures(graph, dir, false);
		}
		else
		{
			qWarning(
				"MaterialEditor: '%s' has an unreadable editor graph (%s); rebuilding it from the "
				"material's routes",
				qPrintable(path),
				qPrintable(error.errorString()));
		}
	}

	MaterialOutputNode* output = ResetGraph(submeshIndex, graph);

	// Without a graph, the board is seeded from the material itself. Only the factors survive: the
	// routes name textures but not how the artist arranged the nodes that produced them.
	if (graph.isEmpty() && output != nullptr)
	{
		auto seed          = QJsonObject();
		seed["baseColorR"] = material.baseColorFactor.r;
		seed["baseColorG"] = material.baseColorFactor.g;
		seed["baseColorB"] = material.baseColorFactor.b;
		seed["baseColorA"] = material.baseColorFactor.a;
		seed["metallic"]   = material.metallicFactor;
		seed["roughness"]  = material.roughnessFactor;
		output->load(seed);
	}

	m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].materialPath = path;

	CompileGraph(submeshIndex);
	RefreshActions();
}

void
MaterialEditorWindow::CompileGraph(int submeshIndex)
{
	if (m_Preview == nullptr || m_PreviewScene == nullptr)
		return;
	if (submeshIndex < 0 || submeshIndex >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	DataFlowGraphModel& model = *m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].model;

	const MaterialOutputNode* output = FindOutputNode(model);
	if (output == nullptr)
		return;

	auto desc            = bgl::LoosePbrMaterialDesc();
	desc.baseColorFactor = output->BaseColorFactor();
	desc.metallicFactor  = output->MetallicFactor();
	desc.roughnessFactor = output->RoughnessFactor();

	const auto route = [&](unsigned int channel) {
		const ChannelData::Route wired = output->Route(channel);

		auto out    = bgl::ChannelRouteDesc();
		out.texture = wired.texture;
		out.channel = wired.channel;
		return out;
	};

	for (unsigned int i = 0; i < 4; ++i) desc.baseColor[i] = route(i);
	for (unsigned int i = 0; i < 3; ++i) desc.orm[i] = route(4 + i);
	for (unsigned int i = 0; i < 2; ++i) desc.normal[i] = route(7 + i);

	m_Preview->SetSubmeshMaterial(
		static_cast<uint32_t>(submeshIndex),
		m_PreviewScene->CreateLoosePbrMaterial(desc));
}
