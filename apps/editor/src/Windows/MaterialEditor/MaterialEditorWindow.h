#pragma once

#include <QWidget>

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

#include "Windows/MaterialEditor/MaterialPreviewWindow.h"

class TexturePreviewCache;

class QComboBox;
class QJsonObject;
class QLabel;
class QPointF;
class QPushButton;
class MaterialGraphView;

namespace assetlib
{
	struct BMaterial;
}

namespace QtNodes
{
	class DataFlowGraphModel;
	class DataFlowGraphicsScene;
	class NodeDelegateModelRegistry;
}

struct MaterialEditorWindowDesc
{
	bgl::GraphicsHandle gfx                 = nullptr;
	uint32_t            maxPreviewInstances = 16;
	MaterialPreviewEnv  previewEnv;
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

	// The project's Data directory. Every texture path a `.bmaterial` stores is relative to it, and
	// baked maps are written beneath it. Set when a project opens; until then materials cannot bake.
	void
	SetDataRoot(const QString& dataRoot);

	// Drops every trace of the previous project: the preview returns to the default sphere and each
	// submesh graph is rebuilt empty. Without this a graph keeps the `.bmaterial` path it was bound
	// to, and the next Save writes into the project the user just left.
	void
	Reset();

private:
	// Rebuilds the submesh selector + one graph per submesh from the preview geometry.
	void
	SetPreviewGeometry(const QStringList& submeshNames);

	// Shows the selected submesh's graph (each submesh has its own; switching clears the board).
	void
	SelectSubmesh(int index);

	// Compiles a submesh's graph into a loose material and binds it to that submesh, live.
	void
	CompileGraph(int submeshIndex);

	// Creates a Texture node for a dropped `.ktx2` at `scenePos` in the current graph.
	void
	AddTextureNode(const QString& path, const QPointF& scenePos);

	// --- Material asset I/O -------------------------------------------------------------------

	// Saves the current submesh's graph to its `.bmaterial`, prompting for a path when it has none
	// (or when `saveAs`). A submesh only has a path once it has been saved or opened.
	void
	SaveCurrentMaterial(bool saveAs);

	// What the save dialog should offer for a submesh that has never been saved: the project's
	// Materials directory, under the submesh's name. A submesh that already has a `.bmaterial` needs
	// none of this -- its own path already puts the dialog in the right place.
	[[nodiscard]] QString
	DefaultMaterialPath() const;

	// Composites the current submesh's routed source textures into the optimized triplet, writes the
	// maps beside its `.bmaterial`, and saves it. Requires the material to already have a path.
	void
	BakeCurrentMaterial();

	// Points the previewed `.bmesh`'s submesh at `materialPath` and rewrites the mesh, so the material
	// an artist just saved is the one the mesh loads next time. A no-op for the sphere.
	void
	AttachMaterialToMesh(int submeshIndex, const QString& materialPath);

	// Loads a `.bmaterial` into a submesh's graph, replacing it, and previews the result. `interactive`
	// governs whether a failure raises a dialog: an explicit Open should, the automatic load of the
	// materials a dropped mesh already references should only log.
	void
	OpenMaterialInto(int submeshIndex, const QString& path, bool interactive = true);

	// The current submesh's graph as a `.bmaterial`: the compiled per-channel routes and factors that
	// the runtime consumes, plus the graph itself so reopening restores the authoring state.
	[[nodiscard]] assetlib::BMaterial
	BuildMaterial(int submeshIndex, const QString& materialPath) const;

	// Rebuilds `submeshIndex`'s model + scene from scratch, optionally seeding it from `graph` (an
	// empty object yields a fresh graph holding only the Output node). Returns the new Output node.
	class MaterialOutputNode*
	ResetGraph(int submeshIndex, const QJsonObject& graph);

	// Enables/disables the toolbar for the current selection.
	void
	RefreshActions();

	MaterialEditorWindowDesc m_Desc;

	std::filesystem::path m_DataRoot;  // empty until a project is opened

	bgl::SceneHandle       m_PreviewScene;
	MaterialPreviewWindow* m_Preview = nullptr;

	// Shared by every TextureNode across every submesh graph, so a texture is decoded once.
	TexturePreviewCache* m_TexturePreviews = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;

	// One node graph per submesh of the current preview geometry. `scene` references `model`, so it
	// is declared after it (destroyed first).
	struct SubmeshGraph
	{
		std::unique_ptr<QtNodes::DataFlowGraphModel>    model;
		std::unique_ptr<QtNodes::DataFlowGraphicsScene> scene;

		// The `.bmaterial` this submesh's graph is bound to. Empty until saved or opened -- notably
		// for the default sphere, which is not backed by any asset and so can only ever "Save As".
		QString materialPath;
	};
	std::vector<SubmeshGraph> m_SubmeshGraphs;
	int                       m_CurrentSubmesh = -1;

	QComboBox*         m_SubmeshSelector = nullptr;
	MaterialGraphView* m_GraphView       = nullptr;
	QPushButton*       m_OpenButton      = nullptr;
	QPushButton*       m_SaveButton      = nullptr;
	QPushButton*       m_SaveAsButton    = nullptr;
	QPushButton*       m_BakeButton      = nullptr;
	QLabel*            m_MaterialLabel   = nullptr;  // the bound `.bmaterial`'s file name
};
