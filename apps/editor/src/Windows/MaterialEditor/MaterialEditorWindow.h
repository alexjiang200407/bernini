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
	bgl::GraphicsRef   gfx                 = nullptr;
	bgl::SceneRef      scene               = nullptr;
	uint32_t           maxPreviewInstances = 16;
	MaterialPreviewEnv previewEnv;
};

class MaterialEditorWindow : public QWidget
{
	Q_OBJECT

public:
	explicit MaterialEditorWindow(QWidget* parent = nullptr, MaterialEditorWindowDesc desc = {});
	~MaterialEditorWindow() override;

	void
	SetDataRoot(const QString& dataRoot);

	void
	Reset();

	/**
	 * Whether the mesh already names `materialPath` for the submesh -- in which case Set Default
	 * Material would rewrite the `.bmesh` to say what it already says.
	 *
	 * `boundPath` is what the `.bmesh` names (empty when the submesh is unbound, which is never
	 * "already default"). The two are compared as *files*, not as strings: they reach here by
	 * different routes -- one from a file dialog, one from the mesh's own relative path resolved
	 * against the data root -- and can spell the same file differently.
	 */
	[[nodiscard]] static bool
	IsAlreadyDefault(const QString& boundPath, const QString& materialPath);

	/**
	 * The scene point a graph should be centred on: the middle of its output node, not its corner --
	 * a node centred by its corner hangs off the left of the panel. Empty for a graph with no sink.
	 */
	[[nodiscard]] static std::optional<QPointF>
	OutputCentre(MaterialGraphModel& model);

	/**
	 * A one-per-line listing of the baked textures `material` currently names -- base colour, normal and
	 * ORM -- or an empty string when it names none (never baked, or not a PBR material). An unrouted map
	 * shows as a dash. Shown read-only: the graph authors the routes these are composited from, and the
	 * Content Explorer's Bake is what rewrites them.
	 */
	[[nodiscard]] static QString
	BakedTexturesSummary(const assetlib::BMaterial& material);

	/**
	 * The material files the editor has open, absolute, in no order. Deleting one behind an open graph
	 * would not stick: the graph still holds it, and the next Save writes it straight back.
	 */
	[[nodiscard]] QStringList
	OpenMaterialPaths() const;

	/**
	 * Re-reads the open material from disk, for a caller that has just rewritten one -- the Content
	 * Explorer's Bake. The graph is authored here and is not what changed; the panel's staleness marker
	 * and baked-texture listing are read off the file, so nothing else would notice until the next time
	 * the user touched a control.
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
	WatchOutputNode(int submeshIndex);

	/** Scrolls the graph view to the current submesh's output node. The sink is what you author back
	 *  from, so it is where a freshly opened or freshly loaded graph should start. */
	void
	CenterOnOutput();

	void
	CompileGraph(int submeshIndex);

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

	[[nodiscard]] QString
	DefaultMaterialPath() const;

	void
	AttachMaterialToMesh(int submeshIndex, const QString& materialPath);

	void
	OpenMaterialInto(int submeshIndex, const QString& path, bool interactive = true);

	[[nodiscard]] assetlib::BMaterial
	BuildMaterial(int submeshIndex, const QString& materialPath) const;

	class MaterialOutputNode*
	ResetGraph(int submeshIndex, const QJsonObject& graph);

	void
	RefreshActions();

	MaterialEditorWindowDesc m_Desc;

	std::filesystem::path m_DataRoot;

	MaterialPreviewWindow* m_Preview = nullptr;

	TexturePreviewCache* m_TexturePreviews = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;

	struct SubmeshGraph
	{
		std::unique_ptr<MaterialGraphModel> model;
		std::unique_ptr<MaterialGraphScene> scene;

		QString materialPath;

		// The live material this graph is previewed through. Created once and rewritten in place on
		// every edit, rather than created anew: a graph compiles on each keystroke, and the scene's
		// loose-material buffer is a fixed-size slot pool.
		bgl::MaterialHandle preview;
	};
	std::vector<SubmeshGraph> m_SubmeshGraphs;
	int                       m_CurrentSubmesh = -1;

	QComboBox*         m_SubmeshSelector    = nullptr;
	QComboBox*         m_OutputSelector     = nullptr;
	MaterialGraphView* m_GraphView          = nullptr;
	QPushButton*       m_OpenButton         = nullptr;
	QPushButton*       m_SaveButton         = nullptr;
	QPushButton*       m_SaveAsButton       = nullptr;
	QPushButton*       m_SetDefaultButton   = nullptr;
	QLabel*            m_MaterialLabel      = nullptr;
	QLabel*            m_BakedTexturesLabel = nullptr;
};
