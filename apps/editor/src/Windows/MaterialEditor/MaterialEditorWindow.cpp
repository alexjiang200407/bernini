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
#include <assetlib/material_bake.h>

#include "Async/BackgroundTask.h"
#include "Project/Project.h"
#include "Thumbnails/TexturePreviewCache.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphScene.h"
#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

using QtNodes::NodeDelegateModelRegistry;

namespace
{
	struct OutputType
	{
		const char* label;
		const char* modelName;
	};

	constexpr std::array<OutputType, 2> c_OutputTypes = { {
		{ "Opaque", "MaterialOutput" },
		{ "Alpha Tested", "AlphaTestedMaterialOutput" },
	} };

	// A `.bmaterial`'s texture references are relative to the project's Data root -- not to the
	// material file -- so a material names `textures_src/tex1.ktx2` and `Textures/orm_ab12.ktx2`
	// whatever directory it lives in. Texture nodes hold absolute paths while the graph is live, so the
	// saved graph is rewritten on the way out and back in. An empty `dir` (no project open) leaves the
	// path alone, as do paths that cannot be expressed relative to it -- a different drive, say. Both
	// stay absolute: still correct, merely not relocatable.
	QString
	Rebase(const QString& path, const std::filesystem::path& dir, bool toRelative)
	{
		if (path.isEmpty() || dir.empty())
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
	m_BakeButton   = new QPushButton(QStringLiteral("Bake"), leftPanel);
	m_BakeButton->setToolTip(QStringLiteral(
		"Composite the routed source textures into the optimized "
		"baseColor / orm / normal maps"));

	m_SetDefaultButton = new QPushButton(QStringLiteral("Set Default Material"), leftPanel);
	m_SetDefaultButton->setToolTip(QStringLiteral(
		"Bind this material to the submesh in the .bmesh, so every instance of the mesh loads with "
		"it.\nThe preview only overrides the instances in front of you until you do."));

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
	connect(m_BakeButton, &QPushButton::clicked, this, &MaterialEditorWindow::BakeCurrentMaterial);
	connect(m_SetDefaultButton, &QPushButton::clicked, this, [this]() {
		SetDefaultMaterial(m_CurrentSubmesh);
	});

	// Names the `.bmaterial` the selected submesh is bound to, so it is clear what Save writes to.
	m_MaterialLabel = new QLabel(leftPanel);
	m_MaterialLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_MaterialLabel->setStyleSheet("color: gray;");

	toolbar->addWidget(m_OpenButton);
	toolbar->addWidget(m_SaveButton);
	toolbar->addWidget(m_SaveAsButton);
	toolbar->addWidget(m_BakeButton);
	toolbar->addWidget(m_SetDefaultButton);
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

	// The graph's sink, chosen rather than dragged in: a material has exactly one, and which one it is
	// *is* the alpha mode. The context menu does not offer them (see MaterialGraphScene).
	m_OutputSelector = new QComboBox(leftPanel);
	for (const OutputType& type : c_OutputTypes)
		m_OutputSelector->addItem(QLatin1String(type.label));
	m_OutputSelector->setEnabled(false);
	m_OutputSelector->setToolTip(QStringLiteral(
		"Alpha Tested adds a base-color alpha input and a cutoff: pixels below it are "
		"discarded"));
	connect(
		m_OutputSelector,
		&QComboBox::
			activated,  // activated, not currentIndexChanged: only a user's pick swaps the sink
		this,
		&MaterialEditorWindow::SetOutputType);

	auto* selectors = new QHBoxLayout();
	selectors->setContentsMargins(0, 0, 0, 0);
	selectors->addWidget(m_SubmeshSelector, 1);
	selectors->addWidget(new QLabel(QStringLiteral("Output"), leftPanel));
	selectors->addWidget(m_OutputSelector);
	leftLayout->addLayout(selectors);

	m_GraphView = new MaterialGraphView(leftPanel);  // its scene is set per selected submesh
	leftLayout->addWidget(m_GraphView);
	connect(
		m_GraphView,
		&MaterialGraphView::TextureDropped,
		this,
		&MaterialEditorWindow::AddTextureNode);

	// Model Preview. It renders the editor's shared Scene through a SceneView of its own, so the
	// geometry pools are sized once (in config.json) rather than split across two scenes.
	QWidget* rightPanel = nullptr;
	if (m_Desc.gfx && m_Desc.scene)
	{
		auto rtDesc         = RenderTargetWindowDesc();
		rtDesc.gfx          = m_Desc.gfx;
		rtDesc.scene        = m_Desc.scene;
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

	m_TexturePreviews = new TexturePreviewCache(this);

	m_Registry            = std::make_shared<NodeDelegateModelRegistry>();
	bgl::IScene* scene    = m_Desc.scene ? m_Desc.scene.Get() : nullptr;
	auto*        previews = m_TexturePreviews;
	m_Registry->registerModel<TextureNode>(
		[scene, previews]() { return std::make_unique<TextureNode>(scene, previews); },
		"Input");
	// Registered so the graph can create one by name and restore one from a saved graph -- but hidden
	// from the context menu, because a sink is switched, not added.
	m_Registry->registerModel<MaterialOutputNode>(
		[]() { return std::make_unique<MaterialOutputNode>(); },
		QLatin1String(c_OutputCategory));
	m_Registry->registerModel<AlphaTestedMaterialOutputNode>(
		[]() { return std::make_unique<AlphaTestedMaterialOutputNode>(); },
		QLatin1String(c_OutputCategory));

	splitter->addWidget(leftPanel);
	splitter->addWidget(rightPanel);

	// The preview should start with a good share of the width (~38%)
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
	ReleasePreviewMaterials();

	// Detach the view before the per-submesh scenes/models are destroyed, so the view never holds a
	// dangling scene pointer during teardown.
	if (m_GraphView)
		m_GraphView->setScene(nullptr);
}

MaterialOutputNode*
MaterialEditorWindow::ResetGraph(int submeshIndex, const QJsonObject& graph)
{
	SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)];

	if (m_CurrentSubmesh == submeshIndex)
		m_GraphView->setScene(nullptr);

	entry.scene.reset();
	entry.model = std::make_unique<MaterialGraphModel>(m_Registry);
	entry.scene = std::make_unique<MaterialGraphScene>(*entry.model);

	if (graph.isEmpty())
	{
		const QtNodes::NodeId outputId =
			entry.model->addNode(QLatin1String(c_OutputTypes[0].modelName));
		entry.model->setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(220.0, 40.0));
	}
	else
	{
		entry.model->load(graph);
	}

	if (m_CurrentSubmesh == submeshIndex)
		m_GraphView->setScene(entry.scene.get());

	MaterialOutputNode* output = WatchOutputNode(submeshIndex);

	if (m_CurrentSubmesh == submeshIndex)
	{
		SyncOutputSelector();
		CenterOnOutput();
	}

	return output;
}

std::optional<QPointF>
MaterialEditorWindow::OutputCentre(MaterialGraphModel& model)
{
	const QtNodes::NodeId outputId = model.OutputNodeId();
	if (outputId == QtNodes::InvalidNodeId)
		return std::nullopt;

	const QPointF pos  = model.nodeData(outputId, QtNodes::NodeRole::Position).value<QPointF>();
	const QSize   size = model.nodeData(outputId, QtNodes::NodeRole::Size).value<QSize>();

	// A node is only measured once it has a graphics object, so a model with no scene reports -1 x -1.
	// Half of that would centre just off the node's corner, which is worse than its corner.
	if (!size.isValid())
		return pos;

	return pos + QPointF(size.width() * 0.5, size.height() * 0.5);
}

void
MaterialEditorWindow::CenterOnOutput()
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	const SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)];
	if (entry.model == nullptr || m_GraphView->scene() != entry.scene.get())
		return;

	if (const std::optional<QPointF> centre = OutputCentre(*entry.model))
		m_GraphView->centerOn(*centre);
}

MaterialOutputNode*
MaterialEditorWindow::WatchOutputNode(int submeshIndex)
{
	// Recompile whenever anything the material depends on changes. The sink is the only one, and every
	// upstream edit reaches it through setInData.
	MaterialOutputNode* output =
		m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].model->OutputNode();
	if (output != nullptr)
	{
		connect(output, &MaterialOutputNode::Changed, this, [this, submeshIndex]() {
			CompileGraph(submeshIndex);
		});
	}
	return output;
}

void
MaterialEditorWindow::SetOutputType(int comboIndex)
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;
	if (comboIndex < 0 || comboIndex >= static_cast<int>(c_OutputTypes.size()))
		return;

	SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)];

	const QString modelName =
		QLatin1String(c_OutputTypes[static_cast<size_t>(comboIndex)].modelName);
	if (!entry.model->SetOutputType(modelName))
		return;

	// The old sink took its Changed connection with it, and the new one starts unwatched.
	WatchOutputNode(m_CurrentSubmesh);
	CompileGraph(m_CurrentSubmesh);
	RefreshActions();
}

void
MaterialEditorWindow::SyncOutputSelector()
{
	const bool hasGraph =
		m_CurrentSubmesh >= 0 && m_CurrentSubmesh < static_cast<int>(m_SubmeshGraphs.size());

	m_OutputSelector->setEnabled(hasGraph);
	if (!hasGraph)
		return;

	const MaterialOutputNode* output =
		m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)].model->OutputNode();
	if (output == nullptr)
		return;

	const auto it = std::ranges::find_if(c_OutputTypes, [&output](const OutputType& type) {
		return output->name() == QLatin1String(type.modelName);
	});
	if (it == c_OutputTypes.end())
		return;

	const QSignalBlocker blocker(m_OutputSelector);
	m_OutputSelector->setCurrentIndex(static_cast<int>(std::distance(c_OutputTypes.begin(), it)));
}

void
MaterialEditorWindow::SetPreviewGeometry(const QStringList& submeshNames)
{
	// The preview's instances -- and the overrides naming these materials -- were destroyed before
	// this was emitted, so nothing wears them any more.
	ReleasePreviewMaterials();

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
			OpenMaterialInto(index, materialPath, false);  // compiles the graph it loads
			continue;
		}

		CompileGraph(index);
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
	SyncOutputSelector();
	CenterOnOutput();
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

	// "Save" and "Bake" both need somewhere to write. The default sphere has no backing asset, so they
	// stay disabled there until the graph has been given a path by Save As.
	m_SaveButton->setEnabled(!materialPath.isEmpty());

	// Baking writes into <Data>/Textures and records paths relative to <Data>, so it needs a project.
	m_BakeButton->setEnabled(!materialPath.isEmpty() && !m_DataRoot.empty());
	m_BakeButton->setToolTip(
		m_DataRoot.empty() ?
			QStringLiteral("Open a project first: baked maps go under its Data root") :
			QStringLiteral(
				"Composite the routed source textures into the optimized "
				"baseColor / orm / normal maps"));

	// Binding a submesh needs a saved material to bind, and a `.bmesh` to write it into: the default
	// sphere is procedural and has neither.
	const bool hasMesh = m_Preview != nullptr && !m_Preview->MeshPath().empty();

	const QString boundPath = m_Preview != nullptr ?
	                              m_Preview->SubmeshMaterialPaths().value(m_CurrentSubmesh) :
	                              QString();
	const bool    isDefault = IsAlreadyDefault(boundPath, materialPath);

	m_SetDefaultButton->setEnabled(!materialPath.isEmpty() && hasMesh && !isDefault);
	m_SetDefaultButton->setToolTip(
		isDefault ?
			QStringLiteral("The mesh already uses this material for this submesh") :
			QStringLiteral(
				"Write this material into the mesh, so every instance of it loads with it"));

	if (materialPath.isEmpty())
	{
		m_MaterialLabel->setText(QStringLiteral("(unsaved)"));
		m_MaterialLabel->setToolTip(QString());
		return;
	}

	// Whether the baked maps still match the source textures the graph routes. A material saved but
	// never baked reads as stale, which is what it is: it has no optimized textures yet.
	bool stale = true;
	try
	{
		const auto file = std::filesystem::path(materialPath.toStdWString());
		stale           = assetlib::bakeIsStale(assetlib::loadMaterial(file), m_DataRoot);
	}
	catch (const std::exception& e)
	{
		// The file may not exist yet (Save As has set the path but not written it). Say nothing.
		qWarning("MaterialEditor: could not check bake freshness: %s", e.what());
	}

	// Show the file name, with the full path on hover -- the toolbar is too narrow for a whole path.
	const QString name = QFileInfo(materialPath).fileName();
	m_MaterialLabel->setText(stale ? QStringLiteral("%1 (stale)").arg(name) : name);
	m_MaterialLabel->setStyleSheet(stale ? "color: #c08040;" : "color: gray;");
	m_MaterialLabel->setToolTip(
		stale ? QStringLiteral("%1\nThe baked textures do not match its sources. Bake to update.")
					.arg(materialPath) :
				materialPath);
}

void
MaterialEditorWindow::AddTextureNode(const QString& path, const QPointF& scenePos)
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	MaterialGraphModel& model = *m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)].model;

	const QtNodes::NodeId nodeId = model.addNode(QStringLiteral("Texture"));
	model.setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);

	if (auto* texture = model.delegateModel<TextureNode>(nodeId))
		texture->SetTexturePath(path);
}

assetlib::BMaterial
MaterialEditorWindow::BuildMaterial(int submeshIndex, const QString& materialPath) const
{
	const SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)];

	// Texture references are stored relative to the Data root, not to the material file.
	const std::filesystem::path& dir = m_DataRoot;

	auto material = assetlib::BMaterial();

	material.shadingModel = assetlib::ShadingModel::kPbr;

	// The graph authors per-channel routes, so a material that has never been baked is loose.
	material.mode = assetlib::MaterialMode::kLoose;

	// A material already on disk keeps whatever a previous bake produced: the triplet, its provenance
	// and the mode. Rebuilding purely from the graph would throw the optimized textures away on every
	// Save. If the routes have since changed, the stamps no longer match and the bake reports stale.
	const auto file = std::filesystem::path(materialPath.toStdWString());
	if (std::filesystem::exists(file))
	{
		try
		{
			const assetlib::BMaterial existing = assetlib::loadMaterial(file);
			material.mode                      = existing.mode;
			material.pbr.baseColorTexture      = existing.pbr.baseColorTexture;
			material.pbr.normalTexture         = existing.pbr.normalTexture;
			material.pbr.ormTexture            = existing.pbr.ormTexture;
			material.pbr.routeStamps           = existing.pbr.routeStamps;
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialEditor: could not read the existing material: %s", e.what());
		}
	}

	material.name = QFileInfo(materialPath).completeBaseName().toStdString();

	const MaterialOutputNode* output = entry.model->OutputNode();
	if (output != nullptr)
	{
		assetlib::PbrParams& pbr = material.pbr;

		pbr.baseColorFactor = output->BaseColorFactor();
		pbr.metallicFactor  = output->MetallicFactor();
		pbr.roughnessFactor = output->RoughnessFactor();

		pbr.alphaMode =
			output->IsAlphaTested() ? assetlib::AlphaMode::kMask : assetlib::AlphaMode::kOpaque;
		pbr.alphaCutoff = output->AlphaCutoff();

		for (unsigned int i = 0; i < assetlib::c_LooseChannelCount; ++i)
		{
			const ChannelData::Route wired = output->Route(i);

			pbr.routes[i].texture = Rebase(wired.path, dir, true).toStdString();
			pbr.routes[i].channel = wired.channel;
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
			path.isEmpty() ? DefaultMaterialPath() : path,
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

	// A submesh with no material yet is bound by its first Save -- there is nothing to overwrite, and
	// leaving it unbound would mean saving a material the mesh never references. Once it has one,
	// Save writes only the `.bmaterial`: rebinding the mesh is Set Default Material's job, and doing
	// it here would edit the shared asset every time the user pressed Ctrl+S.
	if (m_Preview != nullptr && m_Preview->SubmeshMaterialPaths().value(m_CurrentSubmesh).isEmpty())
	{
		AttachMaterialToMesh(m_CurrentSubmesh, path);
	}

	RefreshActions();
}

void
MaterialEditorWindow::SetDefaultMaterial(int submeshIndex)
{
	if (submeshIndex < 0 || submeshIndex >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	const QString path = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].materialPath;
	if (path.isEmpty())
		return;  // nothing on disk to point the mesh at; Save first

	AttachMaterialToMesh(submeshIndex, path);
	RefreshActions();
}

bool
MaterialEditorWindow::IsAlreadyDefault(const QString& boundPath, const QString& materialPath)
{
	if (boundPath.isEmpty() || materialPath.isEmpty())
		return false;

	// Not QFileInfo's own comparison: it falls back to canonicalFilePath(), which is *empty* for a
	// file that does not exist -- so two different missing paths compare equal, and a material whose
	// file has been deleted would grey the button out on any mesh.
	//
	// weakly_canonical resolves the part of the path that does exist and normalises the rest, so it
	// works either way. Case-insensitively, because this is a Windows tool (see the editor CLAUDE.md).
	const auto normalise = [](const QString& path) {
		const auto source = std::filesystem::path(path.toStdWString());

		std::error_code ec;
		const auto      resolved = std::filesystem::weakly_canonical(source, ec);

		return QString::fromStdWString(
			ec ? source.lexically_normal().wstring() : resolved.wstring());
	};

	return normalise(boundPath).compare(normalise(materialPath), Qt::CaseInsensitive) == 0;
}

QString
MaterialEditorWindow::DefaultMaterialPath() const
{
	const QString name = m_SubmeshSelector->currentText();

	if (m_DataRoot.empty())
		return name;

	auto dir = m_DataRoot / Project::c_MaterialsDirectoryName;

	std::error_code ec;
	if (!std::filesystem::is_directory(dir, ec))
		dir = m_DataRoot;

	return QString::fromStdWString((dir / name.toStdWString()).wstring());
}

void
MaterialEditorWindow::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = std::filesystem::path(dataRoot.toStdWString());

	// The preview resolves a mesh's material references against the same root.
	if (m_Preview)
		m_Preview->SetDataRoot(m_DataRoot);

	RefreshActions();
}

void
MaterialEditorWindow::Reset()
{
	// ShowDefaultSphere clears the preview's geometry, mesh path and material paths, then emits
	// GeometryChanged -- which is what rebuilds the graphs, empty, one per submesh.
	if (m_Preview)
	{
		m_Preview->ShowDefaultSphere();
		return;
	}

	// No graphics device, so there is no preview to drive the rebuild.
	m_SubmeshSelector->clear();
	m_GraphView->setScene(nullptr);
	m_SubmeshGraphs.clear();
	m_CurrentSubmesh = -1;
	RefreshActions();
}

void
MaterialEditorWindow::BakeCurrentMaterial()
{
	if (m_CurrentSubmesh < 0 || m_CurrentSubmesh >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	const QString path = m_SubmeshGraphs[static_cast<size_t>(m_CurrentSubmesh)].materialPath;
	if (path.isEmpty() || m_DataRoot.empty())
		return;  // nowhere to write the maps; Save As first, inside a project

	const auto materialPath = std::filesystem::path(path.toStdWString());

	auto desc     = assetlib::MaterialBakeDesc();
	desc.dataRoot = m_DataRoot;

	// Read off the live graph, so the bake reflects the board as it is right now rather than whatever
	// was last written to disk. It has to happen here, on the UI thread: the worker below may not touch
	// the node models.
	auto material = assetlib::BMaterial();
	try
	{
		material = BuildMaterial(m_CurrentSubmesh, path);
	}
	catch (const std::exception& e)
	{
		qWarning(
			"MaterialEditor: failed to compile '%s' for baking: %s",
			qPrintable(path),
			e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake Material"),
			QStringLiteral("Could not bake the material:\n\n%1").arg(QString::fromUtf8(e.what())));
		return;
	}

	const QString name = QFileInfo(path).fileName();

	// A bake decodes, resizes and re-encodes a KTX2 for each of the three maps, which used to freeze
	// the editor for as long as it took. It touches files only, never bgl, so it belongs on a worker.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Baking %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Compositing maps...");
			assetlib::bakeMaterial(material, desc, progress.Cancellation());

			progress.Report(0, 0, "Writing material...");
			assetlib::saveMaterial(material, materialPath);
		},
		background::Cancellable::kYes);

	// Nothing to undo on a cancel: bakeMaterial leaves `material` untouched unless every map was
	// produced, and a map it did write is named by the hash of its inputs -- so it is a correct,
	// reusable file that the next bake picks up as already up to date, and Clean Unused Textures
	// sweeps if it never is.
	if (result.Cancelled())
		return;

	if (result.Failed())
	{
		qWarning(
			"MaterialEditor: failed to bake '%s': %s",
			qPrintable(path),
			qPrintable(result.error));
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake Material"),
			QStringLiteral("Could not bake the material:\n\n%1").arg(result.error));
		return;
	}

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

		// Like every asset reference, relative to the data root -- not to the mesh file.
		const std::string relative = Rebase(materialPath, m_DataRoot, true).toStdString();

		if (assetlib::attachMaterial(mesh, source, relative))
			assetlib::save(mesh, meshPath);

		// The mesh names it now, so the preview's cached bindings must say so too -- otherwise the
		// next Save would still see this submesh as unbound and rewrite the `.bmesh` again.
		m_Preview->SetSubmeshMaterialPath(static_cast<uint32_t>(submeshIndex), materialPath);
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

	const std::filesystem::path& dir = m_DataRoot;

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
		seed["baseColorR"] = material.pbr.baseColorFactor.r;
		seed["baseColorG"] = material.pbr.baseColorFactor.g;
		seed["baseColorB"] = material.pbr.baseColorFactor.b;
		seed["baseColorA"] = material.pbr.baseColorFactor.a;
		seed["metallic"]   = material.pbr.metallicFactor;
		seed["roughness"]  = material.pbr.roughnessFactor;
		output->load(seed);
	}

	m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].materialPath = path;

	CompileGraph(submeshIndex);
	RefreshActions();
}

void
MaterialEditorWindow::CompileGraph(int submeshIndex)
{
	if (m_Preview == nullptr || m_Desc.scene == nullptr)
		return;
	if (submeshIndex < 0 || submeshIndex >= static_cast<int>(m_SubmeshGraphs.size()))
		return;

	const MaterialOutputNode* output =
		m_SubmeshGraphs[static_cast<size_t>(submeshIndex)].model->OutputNode();
	if (output == nullptr)
		return;

	auto desc            = bgl::LoosePbrMaterialDesc();
	desc.baseColorFactor = output->BaseColorFactor();
	desc.metallicFactor  = output->MetallicFactor();
	desc.roughnessFactor = output->RoughnessFactor();

	desc.layerType = output->IsAlphaTested() ? bgl::LayerType::kAlphaTest : bgl::LayerType::kOpaque;
	desc.alphaCutoff = output->AlphaCutoff();

	const auto route = [&](unsigned int channel) {
		const ChannelData::Route wired = output->Route(channel);

		auto out    = bgl::ChannelRouteDesc();
		out.texture = wired.texture;
		out.channel = wired.channel;
		return out;
	};

	// The channel runs come from BMaterial.h, which owns the `routes` array a graph is saved into.
	// A literal offset here would silently disagree with the baker the moment a channel is added.
	const auto channel = [](const assetlib::ChannelGroup& group, size_t component) {
		return static_cast<unsigned int>(assetlib::ChannelIndex(group, component));
	};

	for (size_t i = 0; i < desc.baseColor.size(); ++i)
		desc.baseColor[i] = route(channel(assetlib::c_BaseColorChannels, i));
	for (size_t i = 0; i < desc.orm.size(); ++i)
		desc.orm[i] = route(channel(assetlib::c_OrmChannels, i));
	for (size_t i = 0; i < desc.normal.size(); ++i)
		desc.normal[i] = route(channel(assetlib::c_NormalChannels, i));

	SubmeshGraph& entry = m_SubmeshGraphs[static_cast<size_t>(submeshIndex)];

	// An in-place rewrite keeps the handle, so the instances already overriding with it follow the
	// edit with no rebinding -- but only while the PSO bucket is unchanged. The bucket comes from the
	// handle's layer, which an update cannot rewrite, so flipping the sink between Material Output
	// and Alpha Tested Material Output needs a new material or the cutout would not cut.
	if (entry.preview.IsValid() && entry.preview.layerType == desc.layerType)
	{
		m_Desc.scene->UpdateLoosePbrMaterial(entry.preview, desc);
		return;
	}

	const bgl::MaterialHandle previous = entry.preview;

	// Bind the replacement before destroying what it replaces: a deleted material leaves its slot to
	// be reused, and an instance still overriding with it would silently wear whatever lands there.
	entry.preview = m_Desc.scene->CreateLoosePbrMaterial(desc);
	m_Preview->SetSubmeshMaterial(static_cast<uint32_t>(submeshIndex), entry.preview);

	if (previous.IsValid())
		m_Desc.scene->DeleteMaterial(previous);
}

void
MaterialEditorWindow::ReleasePreviewMaterials()
{
	if (m_Desc.scene == nullptr)
		return;

	for (SubmeshGraph& entry : m_SubmeshGraphs)
	{
		if (!entry.preview.IsValid())
			continue;

		try
		{
			m_Desc.scene->DeleteMaterial(entry.preview);
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialEditor: could not release a preview material: %s", e.what());
		}

		entry.preview = {};
	}
}
