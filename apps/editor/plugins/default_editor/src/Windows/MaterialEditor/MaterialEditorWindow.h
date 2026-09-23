#pragma once

#include <gamelib/AssetManager.h>

#include <QWidget>

#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IEditorHost.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <qcontainerfwd.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <string>
#include <vector>

#include "Windows/MaterialEditor/MaterialGraphSet.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/MaterialEditor/material_editor_ui.h"
#include "Windows/MaterialEditor/material_overrides.h"

class TexturePreviewCache;

class QAction;
class QComboBox;
class QListWidget;
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
	editor::ViewportDesc viewport;
	MaterialPreviewEnv   previewEnv;
};

class MaterialEditorWindow : public editor::EditorPanel
{
	Q_OBJECT

public:
	MaterialEditorWindow(editor::IEditorHost& host, QWidget* parent, MaterialEditorWindowDesc desc);
	std::vector<std::string>
	GetHeldAssets() const override;
	bool
	CanClose() override
	{
		return true;
	}
	void
	SetActive(bool active) override;
	void
	OnAssetChanged(std::string_view key) override;
	~MaterialEditorWindow() override;

	// Fixed for this panel's project lifetime.
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
	 * SetActive drives this; minimizing the host window does not deactivate its selected panel.
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
	GetHeldOpenPaths() const;

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

	/**
	 * Shows the panel's Layer section for a surface board and fills it from the sink, or hides it
	 * for a PBR one, whose layer is the Output selector's sink choice (ADR-9). Also called on the
	 * sink's Changed, so a loaded or seeded document reaches the panel.
	 */
	void
	SyncLayerSection();

	/** The current graph's sink as a surface sink, or null while the board is a PBR one. */
	[[nodiscard]] class SurfaceOutputNode*
	CurrentSurfaceSink() const;

	class MaterialSinkNode*
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
	 * Writes the look the Material combo shows as `submeshIndex`'s default -- into the mesh's
	 * import document, or into the `.bmesh` itself for a mesh with no source -- so every instance
	 * of that mesh, in the preview, in a level and in the game, picks it up on load.
	 *
	 * The deliberate act the preview's instance overrides exist to keep separate from authoring.
	 */
	void
	MakeShownMaterialDefault(int submeshIndex);

	/** Registers another look for the selected submesh, copied from the one on the board. */
	void
	AddMaterialOverride();

	/** Unregisters the shown override and goes back to the submesh's default. */
	void
	RemoveShownMaterialOverride();

	/** Renames the shown override, keeping the material it names. */
	void
	RenameShownMaterialOverride();

	/** Puts `materialPath` on the board and on the previewed submesh, sharing a graph already open
	 *  for it. An empty path leaves the submesh its own blank graph. */
	void
	ShowMaterialForSubmesh(int submeshIndex, const QString& materialPath);

	/** Fills the Material list for the selected submesh and selects the look on the board. */
	void
	RefreshMaterialList();

	/** The look at `row` of the Material list: empty for the default row, which is row 0. */
	[[nodiscard]] QString
	OverrideAtRow(int row) const;

	/** Re-reads the mesh's registered looks into `m_Registered`, one entry per panel submesh. */
	void
	ReloadRegisteredMaterials();

	/** The looks the mesh registers for `submeshIndex`, or none for a mesh that has no file. */
	[[nodiscard]] std::vector<editor::RegisteredMaterial>
	RegisteredMaterialsFor(int submeshIndex) const;

	/** Those of them the list shows: the default row already stands for the one it names. */
	[[nodiscard]] std::vector<editor::RegisteredMaterial>
	ListedMaterialsFor(int submeshIndex) const;

	/** The override shown for `submeshIndex`, or empty when it shows the submesh's default. */
	[[nodiscard]] QString
	ShownOverride(int submeshIndex) const;

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

	class MaterialSinkNode*
	ResetGraph(int graphIndex, const QJsonObject& graph);

	/** Replaces a submesh's model and scene, letting `build` populate the fresh model -- the core
	 *  ResetGraph and the surface-document seed share. */
	class MaterialSinkNode*
	RebuildGraph(int graphIndex, const std::function<void(class MaterialGraphModel&)>& build);

	void
	RefreshActions();

	editor::IEditorHost&     m_Host;
	MaterialEditorWindowDesc m_Desc;

	std::filesystem::path m_DataRoot;

	MaterialPreviewWindow* m_Preview = nullptr;

	TexturePreviewCache* m_TexturePreviews = nullptr;

	std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_Registry;

	// The Output selector's entries, index-aligned with the combo.
	std::vector<editor::OutputType> m_OutputTypes;

	MaterialGraphSet m_Graphs;

	QComboBox* m_SubmeshSelector = nullptr;
	QComboBox* m_OutputSelector  = nullptr;

	// Which look each submesh is showing: the name of a registered override, or empty for the
	// submesh's default. Indexed by panel submesh, sized with the graphs.
	std::vector<QString> m_ShownOverrides;

	// The mesh's own source, empty for a sourceless one -- which registers no looks, because the
	// document they live in is the one a source has. Read with the looks below.
	std::string m_MeshSourceKey;

	// The mesh's registered looks, per panel submesh. Cached because the combo is refilled on
	// every panel refresh and the answer is a whole `.bmesh` read.
	std::vector<std::vector<editor::RegisteredMaterial>> m_Registered;

	// The built widgets, kept whole for the free functions that take them (FillLayerSection).
	editor::MaterialEditorWidgets m_Ui;
	MaterialGraphView*            m_GraphView         = nullptr;
	QPushButton*                  m_OpenButton        = nullptr;
	QPushButton*                  m_SaveButton        = nullptr;
	QPushButton*                  m_SaveAsButton      = nullptr;
	QPushButton*                  m_SaveAllButton     = nullptr;
	QPushButton*                  m_BakeAllButton     = nullptr;
	QPushButton*                  m_AddOverrideButton = nullptr;
	QPushButton*                  m_RemoveOverride    = nullptr;
	QListWidget*                  m_MaterialList      = nullptr;

	// The list's own actions: its context menu, and the keys it answers to.
	QAction*     m_AddLook            = nullptr;
	QAction*     m_RenameLook         = nullptr;
	QAction*     m_RemoveLook         = nullptr;
	QAction*     m_MakeLookDefault    = nullptr;
	QLabel*      m_MaterialLabel      = nullptr;
	QLabel*      m_BakedTexturesLabel = nullptr;
	QLabel*      m_TangentWarning     = nullptr;
	QPushButton* m_GenerateTangents   = nullptr;
};
