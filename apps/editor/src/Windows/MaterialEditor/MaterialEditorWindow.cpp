#include "MaterialEditorWindow.h"
#include "Mesh/mesh_load.h"

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

#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

#include "Async/BackgroundTask.h"
#include "Render/Renderer.h"
#include "Thumbnails/TexturePreviewCache.h"
#include "Windows/MaterialEditor/MaterialGraphModel.h"
#include "Windows/MaterialEditor/MaterialGraphScene.h"
#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/graph_compiler.h"
#include "Windows/MaterialEditor/material_editor_ui.h"
#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/material_io.h"
#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/TextureNode.h"

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

MaterialEditorWindow::MaterialEditorWindow(QWidget* parent, MaterialEditorWindowDesc desc) :
	QWidget(parent), m_Desc(std::move(desc))
{
	auto* splitter = new QSplitter(Qt::Horizontal, this);

	const editor::MaterialEditorWidgets ui = editor::BuildMaterialEditorUi(splitter);

	m_GraphView          = ui.graphView;
	m_OpenButton         = ui.open;
	m_SaveButton         = ui.save;
	m_SaveAsButton       = ui.saveAs;
	m_SaveAllButton      = ui.saveAll;
	m_BakeAllButton      = ui.bakeAll;
	m_SetDefaultButton   = ui.setDefault;
	m_GenerateTangents   = ui.generateTangents;
	m_SubmeshSelector    = ui.submeshSelector;
	m_OutputSelector     = ui.outputSelector;
	m_MaterialLabel      = ui.materialLabel;
	m_BakedTexturesLabel = ui.bakedTextures;
	m_TangentWarning     = ui.tangentWarning;

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

	connect(m_SetDefaultButton, &QPushButton::clicked, this, [this]() {
		SetDefaultMaterial(m_Graphs.CurrentSubmesh());
	});

	connect(m_GenerateTangents, &QPushButton::clicked, this, [this]() {
		if (m_Preview == nullptr || m_Preview->MeshPath().empty())
			return;

		const std::filesystem::path meshPath = m_Preview->MeshPath();

		// Reloading is what puts the new vertex layout in front of the renderer.
		if (editor::GenerateTangents(this, m_DataRoot, meshPath))
			m_Preview->LoadMesh(meshPath);
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

	connect(
		m_GraphView,
		&MaterialGraphView::TextureDropped,
		this,
		&MaterialEditorWindow::AddTextureNode);

	// Model Preview. It renders the editor's shared Scene through a SceneView of its own, so the
	// geometry pools are sized once (in config.json) rather than split across two scenes.
	QWidget* rightPanel = nullptr;
	if (m_Desc.renderer != nullptr)
	{
		auto rtDesc                   = RenderTargetWindowDesc();
		rtDesc.renderer               = m_Desc.renderer;
		rtDesc.initialInstances       = m_Desc.initialPreviewInstances;
		rtDesc.taaEnabled             = m_Desc.taaEnabled;
		rtDesc.renderScale            = m_Desc.renderScale;
		rtDesc.taaReconstructionWidth = m_Desc.taaReconstructionWidth;

		m_Preview  = new MaterialPreviewWindow(splitter, std::move(rtDesc), m_Desc.previewEnv);
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
	else
	{
		auto* placeholder = new QLabel("No graphics device", splitter);
		placeholder->setAlignment(Qt::AlignCenter);
		placeholder->setStyleSheet("color: gray;");
		rightPanel = placeholder;
	}

	m_TexturePreviews = new TexturePreviewCache(this);

	m_Registry = MakeMaterialNodeRegistry(m_Desc.renderer, m_TexturePreviews);

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
	ReleasePreviewMaterials();

	// Detach the view before the per-submesh scenes/models are destroyed, so the view never holds a
	// dangling scene pointer during teardown.
	if (m_GraphView)
		m_GraphView->setScene(nullptr);
}

MaterialOutputNode*
MaterialEditorWindow::ResetGraph(int graphIndex, const QJsonObject& graph)
{
	MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);

	const bool current = m_Graphs.Current() == graphIndex;
	if (current)
		m_GraphView->setScene(nullptr);

	entry.scene.reset();
	entry.model = std::make_unique<MaterialGraphModel>(m_Registry);
	entry.scene = std::make_unique<MaterialGraphScene>(*entry.model);

	if (graph.isEmpty())
	{
		const QtNodes::NodeId outputId =
			entry.model->addNode(QLatin1String(editor::c_OutputTypes[0].modelName));
		entry.model->setNodeData(outputId, QtNodes::NodeRole::Position, QPointF(220.0, 40.0));
	}
	else
	{
		entry.model->load(graph);
	}

	if (current)
		m_GraphView->setScene(entry.scene.get());

	MaterialOutputNode* output = WatchOutputNode(graphIndex);

	if (current)
	{
		SyncOutputSelector();
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

MaterialOutputNode*
MaterialEditorWindow::WatchOutputNode(int graphIndex)
{
	// Recompile whenever anything the material depends on changes. The sink is the only one, and every
	// upstream edit reaches it through setInData.
	MaterialOutputNode* output = m_Graphs.At(graphIndex).model->OutputNode();
	if (output != nullptr)
	{
		connect(output, &MaterialOutputNode::Changed, this, [this, graphIndex]() {
			CompileGraph(graphIndex);
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
	if (comboIndex < 0 || comboIndex >= static_cast<int>(editor::c_OutputTypes.size()))
		return;

	MaterialGraphSet::Graph& entry = m_Graphs.At(graphIndex);

	const QString modelName =
		QLatin1String(editor::c_OutputTypes[static_cast<size_t>(comboIndex)].modelName);
	if (!entry.model->SetOutputType(modelName))
		return;

	// The old sink took its Changed connection with it, and the new one starts unwatched.
	WatchOutputNode(graphIndex);
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

	const MaterialOutputNode* output = m_Graphs.At(graphIndex).model->OutputNode();
	if (output == nullptr)
		return;

	const auto it =
		std::ranges::find_if(editor::c_OutputTypes, [&output](const editor::OutputType& type) {
			return output->name() == QLatin1String(type.modelName);
		});
	if (it == editor::c_OutputTypes.end())
		return;

	const QSignalBlocker blocker(m_OutputSelector);
	m_OutputSelector->setCurrentIndex(
		static_cast<int>(std::distance(editor::c_OutputTypes.begin(), it)));
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
		return;

	// Switching submesh swaps the blackboard to the graph backing it -- which submeshes sharing a
	// material have in common.
	m_Graphs.SetCurrentSubmesh(index);

	const int graphIndex = m_Graphs.ForSubmesh(index);
	m_GraphView->setScene(graphIndex >= 0 ? m_Graphs.At(graphIndex).scene.get() : nullptr);

	SyncOutputSelector();
	FrameOnOutput();
	RefreshActions();
}

QStringList
MaterialEditorWindow::OpenMaterialPaths() const
{
	return m_Graphs.OpenPaths();
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

	const MaterialOutputNode* output =
		graphIndex >= 0 ? m_Graphs.At(graphIndex).model->OutputNode() : nullptr;

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
		m_BakedTexturesLabel->clear();
		m_BakedTexturesLabel->hide();
		return;
	}

	// Whether the baked maps still match the source textures the graph routes. A material saved but
	// never baked reads as stale, which is what it is: it has no optimized textures yet. Loaded once for
	// both this and the baked-texture listing below.
	bool    stale = true;
	QString bakedSummary;
	if (const assetlib::BMaterial* material = m_Graphs.At(graphIndex).onDisk.Get(materialPath))
	{
		// This is a UI refresh, called from a dozen places and never from inside a handler, so a
		// data root that has gone leaves the pessimistic default rather than throwing out of a slot.
		try
		{
			stale = assetlib::AssetStore(m_DataRoot).BakeIsStale(*material);
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
		assetlib::saveMaterial(
			editor::BuildMaterial(*entry.model, path, m_DataRoot),
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

	// Every graph, not just this one: Save As can put a second graph on a path another already
	// holds, and a stamp cannot separate two writes inside one millisecond.
	m_Graphs.ForgetOnDisk();

	// A submesh with no material yet is bound by its first Save -- there is nothing to overwrite, and
	// leaving it unbound would mean saving a material the mesh never references. Once it has one,
	// Save writes only the `.bmaterial`: rebinding the mesh is Set Default Material's job, and doing
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
			assetlib::saveMaterial(
				editor::BuildMaterial(*entry.model, entry.materialPath, m_DataRoot),
				std::filesystem::path(entry.materialPath.toStdWString()));
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
		// its first write, and one that already has a material is left to Set Default Material.
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
			editor::BakeMaterials(m_DataRoot, relative, progress);
		},
		background::Cancellable::kYes);

	// The panel reads its staleness marker and its baked-texture listing off the file, which the bake
	// has just rewritten -- a cancelled run included, since the files before the cancel are baked.
	RefreshMaterialState();

	if (result.Failed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake All"),
			QStringLiteral("Could not bake:\n\n%1").arg(result.error));
	}
}

void
MaterialEditorWindow::SetDefaultMaterial(int submeshIndex)
{
	const int graphIndex = m_Graphs.ForSubmesh(submeshIndex);
	if (graphIndex < 0)
		return;

	const QString path = m_Graphs.At(graphIndex).materialPath;
	if (path.isEmpty())
		return;  // nothing on disk to point the mesh at; Save first

	if (const QString error = AttachMaterialToMesh(submeshIndex, path); !error.isEmpty())
		QMessageBox::warning(window(), QStringLiteral("Set Default Material"), error);

	RefreshActions();
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
		auto mesh = editor::LoadMeshThroughSeam(m_DataRoot, meshPath);

		// Like every asset reference, relative to the data root -- not to the mesh file.
		const std::string relative = Rebase(materialPath, m_DataRoot, true).toStdString();

		if (assetlib::attachMaterial(mesh, source, relative))
		{
			// A mesh with a recorded source persists a rebind as a document edit: the binding is
			// outside the cache key, so the mesh file is neither rewritten nor staled, and the
			// next load applies the document. Only a sourceless mesh still saves its own file.
			if (mesh.source.key.empty())
				assetlib::save(mesh, meshPath);
			else
				assetlib::rebindSubmeshInDocument(
					m_DataRoot,
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

	MaterialOutputNode* output = ResetGraph(graphIndex, graph);

	// Without a graph, the board is seeded from the material itself. Only the factors survive: the
	// routes name textures but not how the artist arranged the nodes that produced them.
	if (graph.isEmpty() && output != nullptr)
	{
		auto seed            = QJsonObject();
		seed["baseColorR"]   = material.pbr.baseColorFactor.r;
		seed["baseColorG"]   = material.pbr.baseColorFactor.g;
		seed["baseColorB"]   = material.pbr.baseColorFactor.b;
		seed["baseColorA"]   = material.pbr.baseColorFactor.a;
		seed["metallic"]     = material.pbr.metallicFactor;
		seed["roughness"]    = material.pbr.roughnessFactor;
		seed["transmission"] = material.pbr.transmissionFactor;
		seed["specularR"]    = material.pbr.specularColorFactor.r;
		seed["specularG"]    = material.pbr.specularColorFactor.g;
		seed["specularB"]    = material.pbr.specularColorFactor.b;
		seed["specular"]     = material.pbr.specularFactor;
		output->load(seed);
	}

	m_Graphs.At(graphIndex).materialPath = path;

	CompileGraph(graphIndex);
	RefreshActions();
}

void
MaterialEditorWindow::CompileGraph(int graphIndex)
{
	if (m_Preview == nullptr || m_Desc.renderer == nullptr)
		return;
	if (!m_Graphs.Holds(graphIndex))
		return;

	editor::CompilePreviewMaterial(m_Graphs.At(graphIndex), *m_Desc.renderer, *m_Preview);
}

void
MaterialEditorWindow::ReleasePreviewMaterials()
{
	if (m_Desc.renderer == nullptr)
		return;

	// Synchronous: the graphs must not be drawn after this returns, and the render loop draws on the
	// same thread this runs on, so the deletes land between frames.
	m_Desc.renderer->Invoke([&] {
		for (MaterialGraphSet::Graph& entry : m_Graphs.All())
		{
			if (!entry.preview.IsValid())
				continue;

			try
			{
				m_Desc.renderer->GetScene()->DeleteMaterial(entry.preview);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialEditor: could not release a preview material: %s", e.what());
			}

			entry.preview = {};
		}
	});
}
