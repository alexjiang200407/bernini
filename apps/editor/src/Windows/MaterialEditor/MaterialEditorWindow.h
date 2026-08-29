#pragma once

#include <QWidget>

#include "util/follows_project.h"
#include "util/held_open_assets.h"

#include <bgl/IGraphics.h>
#include <bgl/IScene.h>

#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"

class TexturePreviewCache;

class QComboBox;
class QJsonObject;
class QLabel;
class QPointF;
class QPushButton;
class MaterialGraphModel;
class MaterialGraphScene;
class MaterialGraphView;

namespace assetlib
{
	struct BMaterial;
}

namespace QtNodes
{
	class NodeDelegateModelRegistry;
}

struct MaterialEditorWindowDesc
{
	Renderer*          renderer                = nullptr;
	uint32_t           initialPreviewInstances = 16;
	bool               taaEnabled              = true;
	float              renderScale             = 1.0f;
	float              taaReconstructionWidth  = 0.4f;
	MaterialPreviewEnv previewEnv;

	// Builds the preview viewport without a native window. See RenderTargetWindowDesc.
	bool     headless       = false;
	uint32_t headlessWidth  = 256;
	uint32_t headlessHeight = 256;
};

class MaterialEditorWindow :
	public QWidget,
	public editor::IHoldsAssets,
	public editor::IFollowsProject
{
	Q_OBJECT

public:
	explicit MaterialEditorWindow(QWidget* parent = nullptr, MaterialEditorWindowDesc desc = {});
	~MaterialEditorWindow() override;

	void
	SetDataRoot(const QString& dataRoot) override;

	// The project Data root this panel resolves against, empty until a project opens.
	[[nodiscard]] const std::filesystem::path&
	GetDataRoot() const noexcept
	{
		return m_DataRoot;
	}

	/** Back to the default sphere and a blank graph, dropping whatever was open. */
	void
	Reset();

	/**
	 * Leaving the panel resets it: the dock's tab switching away (or the dock closing) puts the
	 * default sphere back, which drops the open materials and every held-open path with them.
	 * MainWindow drives this from QDockWidget::visibilityChanged -- a tabified dock's widget gets
	 * no hideEvent on a tab switch.
	 *
	 * Unsaved graph edits go with it. The panel writes nothing until Save, and there is no prompt.
	 */
	void
	SetDockVisible(bool visible);

	/**
	 * The files the panel has open, absolute, in no order: every graph's material, and the mesh the
	 * preview shows them on. Deleting one behind the panel would not stick.
	 */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

	/**
	 * Re-reads the open material from disk, for a caller that has just rewritten one -- a bake, from
	 * here or from the Content Explorer. The graph is authored here and is not what changed; the panel's
	 * staleness marker and baked-texture listing are read off the file, so nothing else would notice
	 * until the next time the user touched a control.
	 */
	void
	RefreshMaterialState();

private:
	void
	SetPreviewGeometry(const QStringList& submeshNames);

	void
	SelectSubmesh(int index);

	void
	SetOutputType(int comboIndex);

	void
	SyncOutputSelector();

	class MaterialOutputNode*
	WatchOutputNode(int graphIndex);

	/** Frames the graph view on the current submesh's output node, at 1:1. The sink is what you author
	 *  back from, so it is where a freshly opened or freshly loaded graph should start. */
	void
	FrameOnOutput();

	/**
	 * Shows or hides the "no tangents" warning for the current submesh: a normal map is authored in a
	 * tangent frame, so one routed onto a submesh without a tangent renders as nothing at all.
	 */
	void
	RefreshTangentWarning();

	void
	CompileGraph(int graphIndex);

	/** Destroys every graph's preview material. The graphs must not be drawn after this. */
	void
	ReleasePreviewMaterials();

	/**
	 * Writes the material at `materialPath` into the `.bmesh` as `submeshIndex`'s default, so every
	 * instance of that mesh -- in the preview, in a level, in the game -- picks it up on load.
	 *
	 * The deliberate act the preview's instance overrides exist to keep separate from authoring.
	 */
	void
	SetDefaultMaterial(int submeshIndex);

	void
	AddTextureNode(const QString& path, const QPointF& scenePos);

	void
	SaveCurrentMaterial(bool saveAs);

	/**
	 * Writes every graph that already has a file, by exactly the rule Save follows -- the mesh binding
	 * a first write leaves included. A graph with no file yet is skipped rather than prompted.
	 *
	 * Reports only what it skipped or could not write: a clean run is reported by the panel it
	 * refreshes.
	 */
	void
	SaveAllMaterials();

	/**
	 * Save All, then composites each of the mesh's distinct materials down to its baked triplet.
	 *
	 * Saving first is not a convenience: a bake reads the routes off disk, so an unsaved edit would
	 * otherwise be baked in its previous state without saying so.
	 */
	void
	BakeAllMaterials();

	/**
	 * Writes `materialPath` into the `.bmesh` as `submeshIndex`'s material.
	 *
	 * @return empty when it landed or when there was no mesh to write to, otherwise what to tell the
	 *         user. The caller says it: a batch must not raise one modal per submesh.
	 */
	[[nodiscard]] QString
	AttachMaterialToMesh(int submeshIndex, const QString& materialPath);

	void
	OpenMaterialInto(int graphIndex, const QString& path, bool interactive = true);

	class MaterialOutputNode*
	ResetGraph(int graphIndex, const QJsonObject& graph);

	void
	RefreshActions();

	MaterialEditorWindowDesc m_Desc;

	std::filesystem::path m_DataRoot;

	MaterialPreviewWindow* m_Preview = nullptr;

	TexturePreviewCache* m_TexturePreviews = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;

	MaterialGraphSet m_Graphs;

	QComboBox*         m_SubmeshSelector    = nullptr;
	QComboBox*         m_OutputSelector     = nullptr;
	MaterialGraphView* m_GraphView          = nullptr;
	QPushButton*       m_OpenButton         = nullptr;
	QPushButton*       m_SaveButton         = nullptr;
	QPushButton*       m_SaveAsButton       = nullptr;
	QPushButton*       m_SaveAllButton      = nullptr;
	QPushButton*       m_BakeAllButton      = nullptr;
	QPushButton*       m_SetDefaultButton   = nullptr;
	QLabel*            m_MaterialLabel      = nullptr;
	QLabel*            m_BakedTexturesLabel = nullptr;
	QLabel*            m_TangentWarning     = nullptr;
	QPushButton*       m_GenerateTangents   = nullptr;
};
