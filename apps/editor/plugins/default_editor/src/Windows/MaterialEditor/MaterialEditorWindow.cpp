#include "MaterialEditorWindow.h"
#include <algorithm>
#include <assetlib/bmaterial.h>
#include <assetlib/bmesh.h>
#include <editor_sdk/material_bake.h>
#include <editor_sdk/mesh_load.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/NodeDelegateModelRegistry>

#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <bgl/IGraphics.h>
#include <bgl/SurfaceType.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qstringview.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphScene.h"
#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/graph_compiler.h"
#include "Windows/MaterialEditor/material_editor_ui.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/material_io.h"
#include "Windows/MaterialEditor/material_overrides.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <assetlib_structs/Node.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/TexturePreviewCache.h>

namespace
{
	/** Whether the graph routes anything into the sink's normal channels. */
	bool
	RoutesNormalMap(const MaterialOutputNode& output)
	{
		for (size_t i = 0; i < assetlib::c_NormalChannels.count; ++i)
		{
			const unsigned int channel =
				static_cast<unsigned int>(assetlib::channelIndex(assetlib::c_NormalChannels, i));

			if (!output.Route(channel).path.isEmpty())
				return true;
		}

		return false;
	}

}

MaterialEditorWindow::MaterialEditorWindow(
	editor::IEditorHost&     host,
	QWidget*                 parent,
	MaterialEditorWindowDesc desc) :
	EditorPanel(parent), m_Host(host), m_Desc(std::move(desc)),
	m_DataRoot(host.GetStore().GetDataRoot())
{
	auto* splitter = new QSplitter(Qt::Horizontal, this);

	const editor::MaterialEditorWidgets ui = editor::BuildMaterialEditorUi(splitter);

	m_GraphView          = ui.graphView;
	m_OpenButton         = ui.open;
	m_SaveButton         = ui.save;
	m_SaveAsButton       = ui.saveAs;
	m_SaveAllButton      = ui.saveAll;
	m_BakeAllButton      = ui.bakeAll;
	m_AddOverrideButton  = ui.addOverride;
	m_RemoveOverride     = ui.removeOverride;
	m_MaterialList       = ui.materialList;
	m_GenerateTangents   = ui.generateTangents;
	m_SubmeshSelector    = ui.submeshSelector;
	m_OutputSelector     = ui.outputSelector;
	m_MaterialLabel      = ui.materialLabel;
	m_BakedTexturesLabel = ui.bakedTextures;
	m_TangentWarning     = ui.tangentWarning;
	m_Ui                 = ui;

	connect(m_OpenButton, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			window(),
			QStringLiteral("Open Material"),
			QString(),
			QStringLiteral("Bernini Material (*.bmaterial)"));
		if (!path.isEmpty())
			OpenMaterialInto(m_Graphs.Current(), path);
	});
	connect(m_SaveButton, &QPushButton::clicked, this, [this]() { SaveCurrentMaterial(false); });
	connect(m_SaveAsButton, &QPushButton::clicked, this, [this]() { SaveCurrentMaterial(true); });
	connect(m_SaveAllButton, &QPushButton::clicked, this, &MaterialEditorWindow::SaveAllMaterials);
	connect(m_BakeAllButton, &QPushButton::clicked, this, &MaterialEditorWindow::BakeAllMaterials);

	// The list's actions are its context menu and its keys, so each one is written once. The strip
	// under the list triggers the same two.
	m_AddLook         = new QAction(QStringLiteral("Add Override..."), this);
	m_RenameLook      = new QAction(QStringLiteral("Rename..."), this);
	m_RemoveLook      = new QAction(QStringLiteral("Remove"), this);
	m_MakeLookDefault = new QAction(QStringLiteral("Make Default"), this);

	m_RenameLook->setShortcut(Qt::Key_F2);
	m_RemoveLook->setShortcut(QKeySequence::Delete);
	for (QAction* action : { m_AddLook, m_RenameLook, m_RemoveLook, m_MakeLookDefault })
	{
		action->setShortcutContext(Qt::WidgetShortcut);
		m_MaterialList->addAction(action);
	}

	connect(m_AddLook, &QAction::triggered, this, &MaterialEditorWindow::AddMaterialOverride);
	connect(
		m_RenameLook,
		&QAction::triggered,
		this,
		&MaterialEditorWindow::RenameShownMaterialOverride);
	connect(
		m_RemoveLook,
		&QAction::triggered,
		this,
		&MaterialEditorWindow::RemoveShownMaterialOverride);
	connect(m_MakeLookDefault, &QAction::triggered, this, [this]() {
		MakeShownMaterialDefault(m_Graphs.CurrentSubmesh());
	});

	connect(m_AddOverrideButton, &QPushButton::clicked, m_AddLook, &QAction::trigger);
	connect(m_RemoveOverride, &QPushButton::clicked, m_RemoveLook, &QAction::trigger);

	// itemActivated, not doubleClicked: Enter on the keyboard is the same gesture, and a list you
	// arrow through should not need the mouse to commit.
	connect(m_MaterialList, &QListWidget::itemActivated, this, [this]() {
		MakeShownMaterialDefault(m_Graphs.CurrentSubmesh());
	});

	connect(
		m_MaterialList,
		&QListWidget::customContextMenuRequested,
		this,
		[this](const QPoint& at) {
			QMenu menu(m_MaterialList);
			menu.addAction(m_AddLook);
			menu.addAction(m_RenameLook);
			menu.addAction(m_RemoveLook);
			menu.addSeparator();
			menu.addAction(m_MakeLookDefault);
			menu.exec(m_MaterialList->viewport()->mapToGlobal(at));
		});

	// currentRowChanged, so the arrow keys move the board exactly as a click does.
	connect(m_MaterialList, &QListWidget::currentRowChanged, this, [this](int row) {
		const int submesh = m_Graphs.CurrentSubmesh();
		if (row < 0 || !m_Graphs.HasSubmesh(submesh) || m_Preview == nullptr)
			return;

		const QString look                             = OverrideAtRow(row);
		m_ShownOverrides[static_cast<size_t>(submesh)] = look;

		const std::vector<editor::RegisteredMaterial> registered = ListedMaterialsFor(submesh);
		const auto found = std::ranges::find(registered, look, &editor::RegisteredMaterial::name);

		ShowMaterialForSubmesh(
			submesh,
			found == registered.end() ? m_Preview->SubmeshMaterialPaths().value(submesh) :
										Rebase(found->material, m_DataRoot, false));
		RefreshActions();
	});

	connect(m_GenerateTangents, &QPushButton::clicked, this, [this]() {
		if (m_Preview == nullptr || m_Preview->MeshPath().empty())
			return;

		const std::filesystem::path meshPath = m_Preview->MeshPath();

		// Reloading is what puts the new vertex layout in front of the renderer.
		if (editor::GenerateTangents(this, m_Host.GetStore(), meshPath))
		{
			m_Preview->LoadMesh(meshPath);
			m_Host.AssetChanged(m_Host.GetStore().KeyFor(meshPath));
		}
	});

	connect(
		m_SubmeshSelector,
		&QComboBox::currentIndexChanged,
		this,
		&MaterialEditorWindow::SelectSubmesh);

	connect(
		m_OutputSelector,
		&QComboBox::
			activated,  // activated, not currentIndexChanged: only a user's pick swaps the sink
		this,
		&MaterialEditorWindow::SetOutputType);

	// The panel edits the surface sink's layer (ADR-9). activated, not currentIndexChanged, for
	// the Output selector's reason: only a user's pick writes, so the sync can set the combo
	// without echoing.
	connect(m_Ui.layerSelector, &QComboBox::activated, this, [this](int index) {
		SurfaceOutputNode* sink = CurrentSurfaceSink();
		if (sink == nullptr || index < 0 || index > static_cast<int>(assetlib::AlphaMode::kHashed))
			return;
		sink->SetAlphaMode(static_cast<assetlib::AlphaMode>(index));
	});

	connect(m_Ui.alphaCutoff, &QDoubleSpinBox::valueChanged, this, [this](double edited) {
		if (SurfaceOutputNode* sink = CurrentSurfaceSink())
			sink->SetAlphaCutoff(static_cast<float>(edited));
	});

	connect(m_Ui.doubleSided, &QCheckBox::toggled, this, [this](bool checked) {
		if (SurfaceOutputNode* sink = CurrentSurfaceSink())
			sink->SetDoubleSided(checked);
	});

	connect(
		m_GraphView,
		&MaterialGraphView::TextureDropped,
		this,
		&MaterialEditorWindow::AddTextureNode);

	// Model Preview. It renders the editor's shared Scene through a SceneView of its own, so the
	// geometry pools are sized once (in config.json) rather than split across two scenes.
	QWidget* rightPanel = nullptr;
	{
		m_Preview = new MaterialPreviewWindow(m_Host, splitter, m_Desc.viewport, m_Desc.previewEnv);
		rightPanel = m_Preview;

		// Dropping a mesh onto the preview swaps its geometry; rebuild the submesh selector.
		connect(m_Preview, &MaterialPreviewWindow::GeometryChanged, this, [this]() {
			SetPreviewGeometry(m_Preview->SubmeshNames());
		});

		// A click in the preview picks through the selector, so the graph swap and the outline
		// both follow; -1 -- empty space -- clears it, and the placeholder reads "No submesh".
		connect(m_Preview, &MaterialPreviewWindow::SubmeshPicked, this, [this](int index) {
			m_SubmeshSelector->setCurrentIndex(index);
		});
	}

	m_TexturePreviews = new TexturePreviewCache(this);

	// The reflected surfaces, copied off the render thread once -- the set is fixed inside
	// CreateGraphics, so this is all of them for the editor's lifetime.
	auto surfaces = std::vector<bgl::SurfaceType>();
	{
		m_Host.InvokeRender([&](editor::RenderContext& context) {
			const std::span<const bgl::SurfaceType> types = context.graphics.GetSurfaceTypes();
			surfaces.assign(types.begin(), types.end());
		});
	}

	m_Registry = MakeMaterialNodeRegistry(&m_Host, m_TexturePreviews, surfaces);

	m_OutputTypes = editor::OutputTypesFor(surfaces);
	for (const editor::OutputType& type : m_OutputTypes) m_OutputSelector->addItem(type.label);

	splitter->addWidget(ui.leftPanel);
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
	if (m_Preview != nullptr)
		m_Preview->SetRenderingEnabled(false);
	ReleasePreviewMaterials();

	// Detach the view before the per-submesh scenes/models are destroyed, so the view never holds a
	// dangling scene pointer during teardown.
	if (m_GraphView)
		m_GraphView->setScene(nullptr);
}

MaterialSinkNode*
MaterialEditorWindow::ResetGraph(int graphIndex, const QJsonObject& graph)
{
	return RebuildGraph(graphIndex, [this, &graph](MaterialGraphModel& model) {
		if (graph.isEmpty())
		{
			const QtNodes::NodeId outputId = model.addNode(m_OutputTypes.front().modelName);
			model.setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(220.0, 40.0));
		}
		else
		{
			model.load(graph);
		}
	});
}

MaterialSinkNode*
MaterialEditorWindow::RebuildGraph(
	int                                             graphIndex,
	const std::function<void(MaterialGraphModel&)>& build)
{
	MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);

	const bool current = m_Graphs.Current() == graphIndex;
	if (current)
		m_GraphView->setScene(nullptr);

	entry.scene.reset();
	entry.model = std::make_unique<MaterialGraphModel>(m_Registry);
	entry.scene = std::make_unique<MaterialGraphScene>(*entry.model);

	build(*entry.model);

	if (current)
		m_GraphView->setScene(entry.scene.get());

	MaterialSinkNode* output = WatchOutputNode(graphIndex);

	if (current)
	{
		SyncOutputSelector();
		SyncLayerSection();
		FrameOnOutput();
	}

	return output;
}

void
MaterialEditorWindow::FrameOnOutput()
{
	const int graphIndex = m_Graphs.Current();
	if (graphIndex < 0)
		return;

	const MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);
	if (entry.model == nullptr || m_GraphView->scene() != entry.scene.get())
		return;

	if (const std::optional<QPointF> centre = OutputCentre(*entry.model))
	{
		// 1:1 rather than a fit to the viewport, which is a few dozen pixels wide while the panel is
		// still being laid out and so measures nothing. Set on every framing, or the graph switched
		// to would open at whatever zoom the last one was left at. Scale first: centerOn is what
		// decides where the view sits, and setupScale moves it.
		m_GraphView->setupScale(1.0);
		m_GraphView->centerOn(*centre);
	}
}

MaterialSinkNode*
MaterialEditorWindow::WatchOutputNode(int graphIndex)
{
	// Recompile whenever anything the material depends on changes. The sink is the only one, and every
	// upstream edit reaches it through setInData.
	MaterialSinkNode* output = m_Graphs.At(graphIndex).model->OutputNode();
	if (output != nullptr)
	{
		connect(output, &MaterialSinkNode::Changed, this, [this, graphIndex]() {
			CompileGraph(graphIndex);

			// A load or a seed changes the sink's layer without touching the panel; the panel
			// follows only while this graph is the one on screen.
			if (graphIndex == m_Graphs.Current())
				SyncLayerSection();
		});
	}
	return output;
}

void
MaterialEditorWindow::SetOutputType(int comboIndex)
{
	const int graphIndex = m_Graphs.Current();
	if (graphIndex < 0)
		return;
	if (comboIndex < 0 || comboIndex >= static_cast<int>(m_OutputTypes.size()))
		return;

	MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);

	const QString& modelName = m_OutputTypes[static_cast<size_t>(comboIndex)].modelName;
	if (!entry.model->SetOutputType(modelName))
		return;

	// The old sink took its Changed connection with it, and the new one starts unwatched.
	WatchOutputNode(graphIndex);
	SyncLayerSection();
	CompileGraph(graphIndex);
	RefreshActions();
}

void
MaterialEditorWindow::SyncOutputSelector()
{
	const int graphIndex = m_Graphs.Current();

	m_OutputSelector->setEnabled(graphIndex >= 0);
	if (graphIndex < 0)
		return;

	const MaterialSinkNode* output = m_Graphs.At(graphIndex).model->OutputNode();
	if (output == nullptr)
		return;

	const auto it = std::ranges::find_if(m_OutputTypes, [&output](const editor::OutputType& type) {
		return output->name() == type.modelName;
	});
	if (it == m_OutputTypes.end())
		return;

	const QSignalBlocker blocker(m_OutputSelector);
	m_OutputSelector->setCurrentIndex(static_cast<int>(std::distance(m_OutputTypes.begin(), it)));
}

SurfaceOutputNode*
MaterialEditorWindow::CurrentSurfaceSink() const
{
	const int graphIndex = m_Graphs.Current();
	if (graphIndex < 0)
		return nullptr;

	const MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);
	if (entry.model == nullptr)
		return nullptr;

	return qobject_cast<SurfaceOutputNode*>(entry.model->OutputNode());
}

void
MaterialEditorWindow::SyncLayerSection()
{
	editor::FillLayerSection(CurrentSurfaceSink(), m_Ui);
}

void
MaterialEditorWindow::SetPreviewGeometry(const QStringList& submeshNames)
{
	// The preview's instances -- and the overrides naming these materials -- were destroyed before
	// this was emitted, so nothing wears them any more.
	ReleasePreviewMaterials();

	m_SubmeshSelector->clear();
	m_GraphView->setScene(nullptr);
	m_Graphs.Reset(static_cast<int>(submeshNames.size()));

	// A new mesh shows every submesh's default; what the previous one was showing means nothing here.
	m_ShownOverrides.assign(static_cast<size_t>(submeshNames.size()), QString());

	const QStringList materialPaths =
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths() : QStringList();

	for (int index = 0; index < submeshNames.size(); ++index)
	{
		m_SubmeshSelector->addItem(submeshNames[index]);

		const QString materialPath = materialPaths.value(index);

		// A submesh naming a material an earlier one already opened joins its graph, so editing that
		// material once updates every submesh wearing it. Only a real file is shared; an unbound
		// submesh gets its own blank graph.
		if (const int shared = materialPath.isEmpty() ? -1 : m_Graphs.FindForPath(materialPath);
		    shared >= 0)
		{
			m_Graphs.Share(shared, index);

			const MaterialGraphSet::Graph& entry = m_Graphs.At(shared);
			if (entry.preview.IsValid())
				m_Preview->SetSubmeshMaterial(static_cast<uint32_t>(index), entry.preview);
			continue;
		}

		const int graphIndex = m_Graphs.Add(index);

		ResetGraph(graphIndex, QJsonObject());

		if (!materialPath.isEmpty() &&
		    std::filesystem::exists(std::filesystem::path(materialPath.toStdWString())))
		{
			OpenMaterialInto(graphIndex, materialPath, false);  // compiles the graph it loads
			continue;
		}

		CompileGraph(graphIndex);
	}

	m_SubmeshSelector->setEnabled(!submeshNames.isEmpty());

	// After the selector is filled, so the looks are indexed by the same submeshes it lists.
	ReloadRegisteredMaterials();

	if (!submeshNames.isEmpty())
		m_SubmeshSelector->setCurrentIndex(0);

	RefreshActions();
}

void
MaterialEditorWindow::SelectSubmesh(int index)
{
	// int with Qt's -1 sentinel at the slot boundary (currentIndexChanged's signature); it becomes
	// an optional here, so a cleared selector explicitly clears the outline too.
	const bool valid = m_Graphs.HasSubmesh(index);

	if (m_Preview != nullptr)
		m_Preview->SetSelectedSubmesh(
			valid ? std::optional(static_cast<uint32_t>(index)) : std::nullopt);

	if (!valid)
	{
		// A cleared selector -- the mesh swapped out, or emptied -- must not leave the previous
		// board's Layer section standing over no graph.
		SyncLayerSection();
		return;
	}

	// Switching submesh swaps the blackboard to the graph backing it -- which submeshes sharing a
	// material have in common.
	m_Graphs.SetCurrentSubmesh(index);

	const int graphIndex = m_Graphs.ForSubmesh(index);
	m_GraphView->setScene(graphIndex >= 0 ? m_Graphs.At(graphIndex).scene.get() : nullptr);

	SyncOutputSelector();
	SyncLayerSection();
	FrameOnOutput();
	RefreshActions();
}

QStringList
MaterialEditorWindow::GetHeldOpenPaths() const
{
	return editor::HeldOpenByMaterialEditor(
		m_Graphs.OpenPaths(),
		m_Preview != nullptr ? m_Preview->MeshPath() : std::filesystem::path());
}

void
MaterialEditorWindow::RefreshMaterialState()
{
	m_Graphs.ForgetOnDisk();
	RefreshActions();
}

void
MaterialEditorWindow::RefreshTangentWarning()
{
	const int graphIndex = m_Graphs.Current();

	const auto* output = qobject_cast<const MaterialOutputNode*>(
		graphIndex >= 0 ? m_Graphs.At(graphIndex).model->OutputNode() : nullptr);

	// Only where it is actionable: a mesh on disk to rewrite, a normal map that is being thrown
	// away, and a submesh that has no tangent to throw it away with.
	const bool missing =
		m_Preview != nullptr && !m_Preview->MeshPath().empty() && m_Graphs.CurrentSubmesh() >= 0 &&
		!m_Preview->SubmeshHasTangent(static_cast<uint32_t>(m_Graphs.CurrentSubmesh())) &&
		output != nullptr && RoutesNormalMap(*output);

	m_TangentWarning->setVisible(missing);
	m_GenerateTangents->setVisible(missing);
}

void
MaterialEditorWindow::RefreshActions()
{
	const int  graphIndex = m_Graphs.Current();
	const bool hasGraph   = graphIndex >= 0;

	m_OpenButton->setEnabled(hasGraph);
	m_SaveAsButton->setEnabled(hasGraph);

	// Both act on the mesh's materials, so neither needs a selection -- but with nothing bound to a
	// file yet there is nothing for either to act on.
	const bool anyBound = !m_Graphs.OpenPaths().isEmpty();
	m_SaveAllButton->setEnabled(anyBound);
	m_BakeAllButton->setEnabled(anyBound);

	const QString materialPath = hasGraph ? m_Graphs.At(graphIndex).materialPath : QString();

	// "Save" needs somewhere to write. The default sphere has no backing asset, so it stays disabled
	// there until the graph has been given a path by Save As.
	m_SaveButton->setEnabled(!materialPath.isEmpty());

	// Binding a submesh needs a saved material to bind, and a `.bmesh` to write it into: the default
	// sphere is procedural and has neither.
	const bool hasMesh = m_Preview != nullptr && !m_Preview->MeshPath().empty();

	const QString boundPath =
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths().value(m_Graphs.CurrentSubmesh()) :
							   QString();
	const bool isDefault = editor::IsSameMaterialFile(boundPath, materialPath);

	RefreshTangentWarning();

	RefreshMaterialList();

	// A look is registered against a submesh of a real mesh, and it is copied from the board, so
	// both need something behind them. A sourceless mesh has no import document to register one in
	// -- it carries its bindings itself -- so it keeps the single default it has.
	const bool canRegister = hasGraph && hasMesh && !m_MeshSourceKey.empty();
	m_AddLook->setEnabled(canRegister);
	m_AddOverrideButton->setEnabled(canRegister);
	m_AddOverrideButton->setToolTip(
		canRegister || !hasMesh ?
			QStringLiteral(
				"Register another look for this submesh, copied from the one on the "
				"board.") :
			QStringLiteral(
				"This mesh was not imported from a source, so it has no import document "
				"to register a look in."));

	// The default row is the mesh's own binding, so there is no registration to rename or remove.
	const bool shownIsOverride = !ShownOverride(m_Graphs.CurrentSubmesh()).isEmpty();
	m_RemoveLook->setEnabled(shownIsOverride);
	m_RenameLook->setEnabled(shownIsOverride);
	m_RemoveOverride->setEnabled(shownIsOverride);
	m_RemoveOverride->setToolTip(
		shownIsOverride ?
			QStringLiteral(
				"Unregister this look. Its .bmaterial stays on disk -- delete it in the "
				"Content Explorer.") :
			QStringLiteral("The default is the mesh's own binding, not a look to unregister."));

	m_MakeLookDefault->setEnabled(!materialPath.isEmpty() && hasMesh && !isDefault);

	if (materialPath.isEmpty())
	{
		m_MaterialLabel->setText(QStringLiteral("(unsaved)"));
		m_MaterialLabel->setToolTip(QString());
		m_BakedTexturesLabel->clear();
		m_BakedTexturesLabel->hide();
		return;
	}

	// Whether the baked maps still match the source textures the graph routes. A material saved but
	// never baked reads as stale, which is what it is: it has no optimized textures yet. Loaded once for
	// both this and the baked-texture listing below.
	bool    stale = true;
	QString bakedSummary;
	if (const assetlib::BMaterial* material =
	        m_Graphs.At(graphIndex).onDisk.Get(m_Host.GetStore(), materialPath))
	{
		// This is a UI refresh, called from a dozen places and never from inside a handler, so a
		// data root that has gone leaves the pessimistic default rather than throwing out of a slot.
		try
		{
			stale = m_Host.GetStore().BakeIsStale(*material);
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialEditor: cannot judge the bake: %s", e.what());
		}

		bakedSummary = editor::BakedTexturesSummary(*material);
	}

	m_BakedTexturesLabel->setText(bakedSummary);
	m_BakedTexturesLabel->setVisible(!bakedSummary.isEmpty());

	// The material's path, word-wrapped in the properties panel. A stale marker says the baked maps no
	// longer match the sources the graph routes.
	m_MaterialLabel->setText(stale ? QStringLiteral("%1 (stale)").arg(materialPath) : materialPath);
	m_MaterialLabel->setStyleSheet(stale ? "color: #c08040;" : "color: gray;");

	// The path leads, because the label clips it once the panel is narrow.
	m_MaterialLabel->setToolTip(
		stale ? QStringLiteral(
					"%1\n\nThe baked textures do not match its sources. Bake All, or the "
					"Content Explorer's Bake, updates them.")
					.arg(materialPath) :
				materialPath);
}

void
MaterialEditorWindow::AddTextureNode(const QString& path, const QPointF& scenePos)
{
	const int graphIndex = m_Graphs.Current();
	if (graphIndex < 0)
		return;

	MaterialGraphModel& model = *m_Graphs.At(graphIndex).model;

	const QtNodes::NodeId nodeId = model.addNode(QStringLiteral("Texture"));
	model.setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);

	if (auto* texture = model.delegateModel<TextureNode>(nodeId))
		texture->SetTexturePath(path);
}

void
MaterialEditorWindow::SaveCurrentMaterial(bool saveAs)
{
	const int graphIndex = m_Graphs.Current();
	if (graphIndex < 0)
		return;

	MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);

	QString path = entry.materialPath;
	if (saveAs || path.isEmpty())
	{
		path = QFileDialog::getSaveFileName(
			window(),
			QStringLiteral("Save Material"),
			path.isEmpty() ?
				editor::DefaultMaterialPath(m_DataRoot, m_SubmeshSelector->currentText()) :
				path,
			QStringLiteral("Bernini Material (*.bmaterial)"));
		if (path.isEmpty())
			return;  // cancelled

		if (QFileInfo(path).suffix().isEmpty())
			path += QStringLiteral(".bmaterial");
	}

	try
	{
		const assetlib::AssetStore& store = m_Host.GetStore();
		store.Save(
			editor::BuildMaterial(*entry.model, path, m_Host.GetStore()),
			store.KeyFor(std::filesystem::path(path.toStdWString())));
		m_Host.AssetChanged(store.KeyFor(std::filesystem::path(path.toStdWString())));
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

	// Every graph, not just this one: Save As can put a second graph on a path another already
	// holds, and a stamp cannot separate two writes inside one millisecond.
	m_Graphs.ForgetOnDisk();

	// A submesh with no material yet is bound by its first Save -- there is nothing to overwrite, and
	// leaving it unbound would mean saving a material the mesh never references. Once it has one,
	// Save writes only the `.bmaterial`: rebinding the mesh is Make Default's job, and doing
	// it here would edit the shared asset every time the user pressed Ctrl+S.
	const int submesh = m_Graphs.CurrentSubmesh();
	if (m_Preview != nullptr && m_Preview->SubmeshMaterialPaths().value(submesh).isEmpty())
	{
		if (const QString error = AttachMaterialToMesh(submesh, path); !error.isEmpty())
			QMessageBox::warning(window(), QStringLiteral("Save Material"), error);
	}

	RefreshActions();
}

void
MaterialEditorWindow::SaveAllMaterials()
{
	auto result = editor::MaterialSaveResult();

	for (MaterialGraphSet::Graph& entry : m_Graphs.All())
	{
		if (entry.model == nullptr)
			continue;

		if (entry.materialPath.isEmpty())
		{
			++result.unsaved;
			continue;
		}

		try
		{
			const assetlib::AssetStore& store = m_Host.GetStore();
			store.Save(
				editor::BuildMaterial(*entry.model, entry.materialPath, m_Host.GetStore()),
				store.KeyFor(std::filesystem::path(entry.materialPath.toStdWString())));
			m_Host.AssetChanged(
				store.KeyFor(std::filesystem::path(entry.materialPath.toStdWString())));
		}
		catch (const std::exception& e)
		{
			qWarning(
				"MaterialEditor: failed to save '%s': %s",
				qPrintable(entry.materialPath),
				e.what());
			result.failed << entry.materialPath;
			continue;
		}

		++result.saved;

		// Save's rule, applied to every submesh the graph drives: one with no material yet is bound by
		// its first write, and one that already has a material is left to Make Default.
		if (m_Preview == nullptr)
			continue;

		for (const uint32_t submesh : entry.submeshes)
		{
			const int index = static_cast<int>(submesh);
			if (!m_Preview->SubmeshMaterialPaths().value(index).isEmpty())
				continue;

			// Whatever stopped the write is the `.bmesh` itself, which every submesh here shares, so
			// the rest would fail the same way -- and a batch must not raise one modal per submesh.
			if (!AttachMaterialToMesh(index, entry.materialPath).isEmpty())
			{
				result.unattached << entry.materialPath;
				break;
			}
		}
	}

	// Every graph, not just the ones written: two graphs can hold one path, and a stamp cannot
	// separate two writes inside one millisecond.
	m_Graphs.ForgetOnDisk();
	RefreshActions();

	if (const QString summary = editor::MaterialSaveSummary(result); !summary.isEmpty())
		QMessageBox::information(window(), QStringLiteral("Save All"), summary);
}

void
MaterialEditorWindow::BakeAllMaterials()
{
	SaveAllMaterials();

	const QStringList files = editor::UniqueMaterialFiles(m_Graphs.OpenPaths());
	if (files.isEmpty())
		return;

	auto relative = QStringList();
	relative.reserve(files.size());
	for (const QString& file : files) relative << Rebase(file, m_DataRoot, true);

	// Compositing decodes, resizes and re-encodes a KTX2 per map, so it runs off the UI thread. It
	// touches files only, never bgl.
	const background::TaskResult result = background::RunWithLoadingScreen(
		window(),
		QStringLiteral("Baking materials"),
		[&](background::Progress& progress) {
			editor::BakeMaterials(m_Host.GetStore(), relative, progress);
		},
		background::Cancellable::kYes);

	// The panel reads its staleness marker and its baked-texture listing off the file, which the bake
	// has just rewritten -- a cancelled run included, since the files before the cancel are baked.
	RefreshMaterialState();
	if (result.Completed())
		for (const QString& key : relative) m_Host.AssetChanged(key.toStdString());

	if (result.Failed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake All"),
			QStringLiteral("Could not bake:\n\n%1").arg(result.error));
	}
}

void
MaterialEditorWindow::MakeShownMaterialDefault(int submeshIndex)
{
	const int graphIndex = m_Graphs.ForSubmesh(submeshIndex);
	if (graphIndex < 0)
		return;

	const QString path = m_Graphs.At(graphIndex).materialPath;
	if (path.isEmpty())
		return;  // nothing on disk to point the mesh at; Save first

	// The look the submesh is giving up: registered before it is replaced, or it leaves the list
	// and nothing in the project names it any more.
	const QString outgoing = editor::NameForOutgoingDefault(
		RegisteredMaterialsFor(submeshIndex),
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths().value(submeshIndex) : QString(),
		m_DataRoot);

	if (!outgoing.isEmpty() && !m_MeshSourceKey.empty())
	{
		try
		{
			const assetlib::AssetStore& store = m_Host.GetStore();
			const auto     mesh   = editor::LoadMeshThroughSeam(store, m_Preview->MeshPath());
			const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submeshIndex));

			store.SetSubmeshMaterialOverrideInDocument(
				mesh.source.key,
				mesh.stringPool.at(mesh.submeshes[source].nameOffset),
				outgoing.toStdString(),
				Rebase(m_Preview->SubmeshMaterialPaths().value(submeshIndex), m_DataRoot, true)
					.toStdString());
		}
		catch (const std::exception& e)
		{
			// Keeping the old look is a courtesy; failing it must not stop the rebind the user
			// asked for.
			qWarning("MaterialEditor: could not keep '%s': %s", qPrintable(outgoing), e.what());
		}
	}

	if (const QString error = AttachMaterialToMesh(submeshIndex, path); !error.isEmpty())
	{
		QMessageBox::warning(window(), QStringLiteral("Make Default"), error);
		return;
	}

	ReloadRegisteredMaterials();

	// The look is the default now, so it is no longer an override being shown -- the combo's first
	// entry is what it is.
	m_ShownOverrides[static_cast<size_t>(submeshIndex)].clear();
	RefreshActions();
}

void
MaterialEditorWindow::AddMaterialOverride()
{
	const int submesh    = m_Graphs.CurrentSubmesh();
	const int graphIndex = m_Graphs.ForSubmesh(submesh);
	if (graphIndex < 0 || m_Preview == nullptr || m_Preview->MeshPath().empty())
		return;

	const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submesh));
	if (source == assetlib::c_InvalidIndex)
		return;

	const std::vector<editor::RegisteredMaterial> registered = RegisteredMaterialsFor(submesh);

	bool          accepted = false;
	const QString name     = QInputDialog::getText(
		window(),
		QStringLiteral("Add Override"),
		QStringLiteral("Name this look. A game asks for it by this name."),
		QLineEdit::Normal,
		QString(),
		&accepted);
	if (!accepted)
		return;

	if (!editor::CanRegisterMaterialName(registered, name))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Add Override"),
			name.trimmed().isEmpty() ?
				QStringLiteral("An override needs a name.") :
				QStringLiteral("This submesh already registers a look called '%1'.").arg(name));
		return;
	}

	const MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);
	const QString path = editor::NewOverrideMaterialPath(m_DataRoot, entry.materialPath, name);

	try
	{
		const assetlib::AssetStore& store = m_Host.GetStore();

		// Read before anything is written: a mesh with no source has no document to register in,
		// and a copy saved first would be a `.bmaterial` nothing names.
		const auto mesh = editor::LoadMeshThroughSeam(store, m_Preview->MeshPath());
		core::throw_runtime_error_if(
			mesh.source.key.empty(),
			"'{}': it was not imported from a source, so it has no import document to register a "
			"look in",
			m_Preview->MeshPath().string());

		// The copy is written before it is registered: a registration naming a file that is not
		// there is one every later load reports as a broken reference.
		const std::string key = store.KeyFor(std::filesystem::path(path.toStdWString()));
		store.Save(editor::BuildMaterial(*entry.model, path, store), key);

		store.SetSubmeshMaterialOverrideInDocument(
			mesh.source.key,
			mesh.stringPool.at(mesh.submeshes[source].nameOffset),
			name.trimmed().toStdString(),
			key);
		m_Host.AssetChanged(key);
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: could not register '%s': %s", qPrintable(name), e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Add Override"),
			QStringLiteral("Could not register the override:\n%1")
				.arg(QString::fromLatin1(e.what())));
		return;
	}

	ReloadRegisteredMaterials();
	m_ShownOverrides[static_cast<size_t>(submesh)] = name.trimmed();
	ShowMaterialForSubmesh(submesh, path);
	RefreshActions();
}

void
MaterialEditorWindow::RemoveShownMaterialOverride()
{
	const int     submesh = m_Graphs.CurrentSubmesh();
	const QString shown   = ShownOverride(submesh);
	if (shown.isEmpty() || m_Preview == nullptr || m_Preview->MeshPath().empty())
		return;

	const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submesh));
	if (source == assetlib::c_InvalidIndex)
		return;

	try
	{
		const assetlib::AssetStore& store = m_Host.GetStore();
		auto mesh = editor::LoadMeshThroughSeam(store, m_Preview->MeshPath());
		store.RemoveSubmeshMaterialOverrideInDocument(
			mesh.source.key,
			mesh.stringPool.at(mesh.submeshes[source].nameOffset),
			shown.toStdString());
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: could not remove '%s': %s", qPrintable(shown), e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Remove Override"),
			QStringLiteral("Could not remove the override:\n%1")
				.arg(QString::fromLatin1(e.what())));
		return;
	}

	// The `.bmaterial` is left on disk: unregistering a look is not deleting an asset, and the
	// Content Explorer is where a file is deleted.
	ReloadRegisteredMaterials();
	m_ShownOverrides[static_cast<size_t>(submesh)].clear();
	ShowMaterialForSubmesh(submesh, m_Preview->SubmeshMaterialPaths().value(submesh));
	RefreshActions();
}

void
MaterialEditorWindow::RenameShownMaterialOverride()
{
	const int     submesh = m_Graphs.CurrentSubmesh();
	const QString shown   = ShownOverride(submesh);
	if (shown.isEmpty() || m_Preview == nullptr || m_Preview->MeshPath().empty())
		return;

	const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submesh));
	if (source == assetlib::c_InvalidIndex)
		return;

	bool          accepted = false;
	const QString name     = QInputDialog::getText(
		window(),
		QStringLiteral("Rename Override"),
		QStringLiteral("Name this look. A game asks for it by this name."),
		QLineEdit::Normal,
		shown,
		&accepted);
	if (!accepted || name.trimmed() == shown)
		return;

	const std::vector<editor::RegisteredMaterial> registered = RegisteredMaterialsFor(submesh);
	const auto found = std::ranges::find(registered, shown, &editor::RegisteredMaterial::name);
	if (found == registered.end())
		return;

	// Every look but this one: renaming it to what it is already called is not a collision.
	auto others = registered;
	std::erase_if(others, [&shown](const editor::RegisteredMaterial& look) {
		return look.name == shown;
	});

	if (!editor::CanRegisterMaterialName(others, name))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Rename Override"),
			name.trimmed().isEmpty() ?
				QStringLiteral("An override needs a name.") :
				QStringLiteral("This submesh already registers a look called '%1'.").arg(name));
		return;
	}

	try
	{
		const assetlib::AssetStore& store = m_Host.GetStore();
		const auto        mesh = editor::LoadMeshThroughSeam(store, m_Preview->MeshPath());
		const std::string submeshName =
			std::string(mesh.stringPool.at(mesh.submeshes[source].nameOffset));

		// Registered under the new name before the old one goes, so a failure between the two
		// leaves the look reachable rather than unregistered.
		store.SetSubmeshMaterialOverrideInDocument(
			mesh.source.key,
			submeshName,
			name.trimmed().toStdString(),
			found->material.toStdString());
		store.RemoveSubmeshMaterialOverrideInDocument(
			mesh.source.key,
			submeshName,
			shown.toStdString());
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: could not rename '%s': %s", qPrintable(shown), e.what());
		QMessageBox::warning(
			window(),
			QStringLiteral("Rename Override"),
			QStringLiteral("Could not rename the override:\n%1")
				.arg(QString::fromLatin1(e.what())));
		return;
	}

	ReloadRegisteredMaterials();
	m_ShownOverrides[static_cast<size_t>(submesh)] = name.trimmed();
	RefreshActions();
}

void
MaterialEditorWindow::ShowMaterialForSubmesh(int submeshIndex, const QString& materialPath)
{
	if (!m_Graphs.HasSubmesh(submeshIndex))
		return;

	// A graph already open for this file is the one to show: two submeshes wearing one material
	// share a graph, so editing it once updates both.
	int graphIndex = materialPath.isEmpty() ? -1 : m_Graphs.FindForPath(materialPath);
	if (graphIndex >= 0)
		m_Graphs.Share(graphIndex, submeshIndex);
	else
	{
		graphIndex = m_Graphs.Add(submeshIndex);
		ResetGraph(graphIndex, QJsonObject());

		if (!materialPath.isEmpty() &&
		    std::filesystem::exists(std::filesystem::path(materialPath.toStdWString())))
			OpenMaterialInto(graphIndex, materialPath, false);  // compiles the graph it loads
		else
			CompileGraph(graphIndex);
	}

	const MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);
	if (m_Preview != nullptr && entry.preview.IsValid())
		m_Preview->SetSubmeshMaterial(static_cast<uint32_t>(submeshIndex), entry.preview);

	m_GraphView->setScene(entry.scene.get());
	SyncOutputSelector();
	SyncLayerSection();
	FrameOnOutput();
}

void
MaterialEditorWindow::ReloadRegisteredMaterials()
{
	m_Registered.assign(static_cast<size_t>(m_SubmeshSelector->count()), {});
	m_MeshSourceKey.clear();

	if (m_Preview == nullptr || m_Preview->MeshPath().empty())
		return;

	try
	{
		const auto mesh = editor::LoadMeshThroughSeam(m_Host.GetStore(), m_Preview->MeshPath());
		m_MeshSourceKey = mesh.source.key;

		for (size_t submesh = 0; submesh < m_Registered.size(); ++submesh)
		{
			const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submesh));
			if (source == assetlib::c_InvalidIndex)
				continue;

			m_Registered[submesh] = editor::RegisteredMaterialsFor(mesh, source);
		}
	}
	catch (const std::exception& e)
	{
		// A UI refresh, so a mesh that will not read leaves the looks unlisted rather than
		// throwing out of a slot; the panel still shows the default the preview loaded.
		qWarning("MaterialEditor: cannot read the registered materials: %s", e.what());
	}
}

std::vector<editor::RegisteredMaterial>
MaterialEditorWindow::RegisteredMaterialsFor(int submeshIndex) const
{
	if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= m_Registered.size())
		return {};

	return m_Registered[static_cast<size_t>(submeshIndex)];
}

std::vector<editor::RegisteredMaterial>
MaterialEditorWindow::ListedMaterialsFor(int submeshIndex) const
{
	return editor::LooksBesidesDefault(
		RegisteredMaterialsFor(submeshIndex),
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths().value(submeshIndex) : QString(),
		m_DataRoot);
}

QString
MaterialEditorWindow::ShownOverride(int submeshIndex) const
{
	if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= m_ShownOverrides.size())
		return {};

	return m_ShownOverrides[static_cast<size_t>(submeshIndex)];
}

QString
MaterialEditorWindow::OverrideAtRow(int row) const
{
	const std::vector<editor::RegisteredMaterial> registered =
		ListedMaterialsFor(m_Graphs.CurrentSubmesh());

	// Row 0 is the submesh's default, so the registered looks start at 1.
	if (row <= 0 || static_cast<size_t>(row) > registered.size())
		return {};

	return registered[static_cast<size_t>(row - 1)].name;
}

void
MaterialEditorWindow::RefreshMaterialList()
{
	const int submesh = m_Graphs.CurrentSubmesh();

	const QSignalBlocker blocker(m_MaterialList);
	m_MaterialList->clear();

	if (!m_Graphs.HasSubmesh(submesh))
	{
		m_MaterialList->setEnabled(false);
		return;
	}

	const QString defaultPath =
		m_Preview != nullptr ? m_Preview->SubmeshMaterialPaths().value(submesh) : QString();

	auto* first = new QListWidgetItem(
		defaultPath.isEmpty() ? QStringLiteral("(unbound)") :
								QFileInfo(defaultPath).completeBaseName(),
		m_MaterialList);
	first->setData(editor::c_IsDefaultMaterialRole, true);
	first->setToolTip(
		defaultPath.isEmpty() ?
			QStringLiteral("This submesh has no material yet. Saving one binds it.") :
			QStringLiteral("%1\n\nEvery instance of this mesh loads with this look.")
				.arg(defaultPath));

	const std::vector<editor::RegisteredMaterial> registered = ListedMaterialsFor(submesh);
	for (const editor::RegisteredMaterial& look : registered)
	{
		auto* item = new QListWidgetItem(look.name, m_MaterialList);
		item->setToolTip(QStringLiteral(
							 "%1\n\nA game wears this look by asking for '%2'. Double-click to "
							 "make it the default.")
		                     .arg(look.material, look.name));
	}

	const QString shown = ShownOverride(submesh);
	const auto    found = std::ranges::find(registered, shown, &editor::RegisteredMaterial::name);
	m_MaterialList->setCurrentRow(
		found == registered.end() ? 0 :
									static_cast<int>(std::distance(registered.begin(), found)) + 1);

	m_MaterialList->setEnabled(true);
}

void
MaterialEditorWindow::Reset()
{
	// The preview's Reset clears its geometry, mesh path and material paths, then emits
	// GeometryChanged -- which is what rebuilds the graphs, empty, one per submesh.
	if (m_Preview)
	{
		m_Preview->Reset();
		return;
	}

	// No graphics device, so there is no preview to drive the rebuild.
	m_SubmeshSelector->clear();
	m_GraphView->setScene(nullptr);
	m_Graphs.Clear();
	RefreshActions();
}

void
MaterialEditorWindow::SetDockVisible(const bool visible)
{
	if (!visible)
		Reset();
}

QString
MaterialEditorWindow::AttachMaterialToMesh(int submeshIndex, const QString& materialPath)
{
	if (m_Preview == nullptr)
		return {};

	const std::filesystem::path meshPath = m_Preview->MeshPath();
	if (meshPath.empty())
		return {};

	const uint32_t source = m_Preview->SourceSubmesh(static_cast<uint32_t>(submeshIndex));
	if (source == assetlib::c_InvalidIndex)
		return {};

	try
	{
		auto mesh = editor::LoadMeshThroughSeam(m_Host.GetStore(), meshPath);

		// Like every asset reference, relative to the data root -- not to the mesh file.
		const std::string relative = Rebase(materialPath, m_DataRoot, true).toStdString();

		if (assetlib::attachMaterial(mesh, source, relative))
		{
			// A mesh with a recorded source persists a rebind as a document edit: the binding is
			// outside the cache key, so the mesh file is neither rewritten nor staled, and the
			// next load applies the document. Only a sourceless mesh still saves its own file.
			if (mesh.source.key.empty())
			{
				const assetlib::AssetStore& meshStore = m_Host.GetStore();
				meshStore.Save(mesh, meshStore.KeyFor(meshPath));
				m_Host.AssetChanged(meshStore.KeyFor(meshPath));
			}
			else
				m_Host.GetStore().RebindSubmeshInDocument(
					mesh.source.key,
					mesh.stringPool.at(mesh.submeshes[source].nameOffset),
					relative);
		}

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

		return QStringLiteral(
				   "The material was saved, but the mesh could not be updated to "
				   "reference it:\n%1")
		    .arg(QString::fromLatin1(e.what()));
	}

	return {};
}

void
MaterialEditorWindow::OpenMaterialInto(int graphIndex, const QString& path, bool interactive)
{
	if (!m_Graphs.Holds(graphIndex))
		return;

	auto material = assetlib::BMaterial();
	try
	{
		const assetlib::AssetStore& store = m_Host.GetStore();
		material                          = store.Load<assetlib::BMaterial>(
			store.KeyFor(std::filesystem::path(path.toStdWString())));
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
	// these routes. A material with no graph was written outside the editor, so its board is rebuilt
	// from the document below instead.
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

	// A surface document never opens behind a PBR board: Save compiles the board, so the board is
	// the surface's or nothing -- the fallback PBR seed would be compiled into a demotion.
	if (material.shadingModel == assetlib::ShadingModel::kPbrSurface)
	{
		const QString sinkName = SurfaceOutputNode::ModelNameFor(material.surface.name);
		if (m_Registry->registeredModelCreators().count(sinkName) == 0)
		{
			// Surfaces are registered once, inside CreateGraphics, from the startup project's
			// shader directory -- a second project's surface, or one added since launch, has no
			// sink to show until the next launch.
			qWarning(
				"MaterialEditor: cannot open '%s': surface '%s' is not registered in this session",
				qPrintable(path),
				material.surface.name.c_str());
			if (interactive)
			{
				QMessageBox::warning(
					window(),
					QStringLiteral("Open Material"),
					QStringLiteral(
						"'%1' is drawn by surface '%2', which this session has not "
						"registered. Open the project that provides it and relaunch.")
						.arg(path, QString::fromStdString(material.surface.name)));
			}
			return;
		}

		if (GraphHoldsNodeType(graph, sinkName))
		{
			ResetGraph(graphIndex, graph);
		}
		else
		{
			// A hand-authored document, or a board saved while surface boards could not be
			// authored -- either way the document is what there is to show.
			RebuildGraph(graphIndex, [this, &material](MaterialGraphModel& model) {
				if (!BuildSurfaceMaterialGraph(model, material, m_DataRoot))
				{
					qWarning(
						"MaterialEditor: could not build the surface board for '%s'",
						material.name.c_str());
				}
			});
		}

		m_Graphs.At(graphIndex).materialPath = path;
		CompileGraph(graphIndex);
		RefreshActions();
		return;
	}

	if (graph.isEmpty())
	{
		RebuildGraph(graphIndex, [this, &material](MaterialGraphModel& model) {
			BuildPbrMaterialGraph(model, material, m_DataRoot);
		});
	}
	else
	{
		ResetGraph(graphIndex, graph);

		// A board saved before the port existed has no wire for the map the document names, and
		// Save compiles the board -- so without this the first save after opening drops the key.
		WireGeometryOcclusion(*m_Graphs.At(graphIndex).model, material, m_DataRoot);
	}

	m_Graphs.At(graphIndex).materialPath = path;

	CompileGraph(graphIndex);
	RefreshActions();
}

void
MaterialEditorWindow::CompileGraph(int graphIndex)
{
	if (m_Preview == nullptr)
		return;
	if (!m_Graphs.Holds(graphIndex))
		return;

	MaterialGraphSet::Graph& graph = m_Graphs.At(graphIndex);

	editor::CompilePreviewMaterial(graph, m_Host, *m_Preview);
}

void
MaterialEditorWindow::ReleasePreviewMaterials()
{
	// Synchronous: the graphs must not be drawn after this returns, and the render loop draws on the
	// same thread this runs on, so the deletes land between frames.
	m_Host.InvokeRender([&](editor::RenderContext& context) {
		for (MaterialGraphSet::Graph& entry : m_Graphs.All())
		{
			if (!entry.preview.IsValid())
				continue;

			try
			{
				context.scene.DeleteMaterial(entry.preview);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialEditor: could not release a preview material: %s", e.what());
			}

			entry.preview = {};
		}
	});
}

std::vector<std::string>
MaterialEditorWindow::GetHeldAssets() const
{
	std::vector<std::string> keys;
	auto                     paths = GetHeldOpenPaths();
	paths.append(m_Preview->GetHeldOpenPaths());
	for (const auto& path : paths)
	{
		try
		{
			keys.push_back(m_Host.GetStore().KeyFor(std::filesystem::path(path.toStdWString())));
		}
		catch (const std::exception&)
		{}  // Configured environments can live outside the project.
	}
	return keys;
}
void
MaterialEditorWindow::SetActive(bool active)
{
	SetDockVisible(active);
	m_Preview->SetRenderingEnabled(active);
}
void
MaterialEditorWindow::OnAssetChanged(std::string_view)
{
	RefreshMaterialState();
}
