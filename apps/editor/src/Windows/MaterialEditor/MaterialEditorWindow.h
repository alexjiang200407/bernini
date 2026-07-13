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
	bgl::GraphicsHandle gfx                 = nullptr;
	bgl::SceneHandle    scene               = nullptr;
	uint32_t            maxPreviewInstances = 16;
	MaterialPreviewEnv  previewEnv;
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
	BakeCurrentMaterial();

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

	QComboBox*         m_SubmeshSelector  = nullptr;
	QComboBox*         m_OutputSelector   = nullptr;
	MaterialGraphView* m_GraphView        = nullptr;
	QPushButton*       m_OpenButton       = nullptr;
	QPushButton*       m_SaveButton       = nullptr;
	QPushButton*       m_SaveAsButton     = nullptr;
	QPushButton*       m_BakeButton       = nullptr;
	QPushButton*       m_SetDefaultButton = nullptr;
	QLabel*            m_MaterialLabel    = nullptr;
};
