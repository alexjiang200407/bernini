#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QWidget>
#include <assetlib/blend.h>
#include <cstdint>
#include <gamelib/BlendSpaceInfo.h>
#include <qcontainerfwd.h>
#include <qtmetamacros.h>
#include <vector>

#include "Render/environment.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/follows_project.h"
#include "util/held_open_assets.h"

class AnimationPreviewWindow;
class QComboBox;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QHideEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QToolButton;
class Scrubber;

namespace game
{
	class AssetManager;
}

/**
 * The Blend Space Editor: one `.bblend` open, its spaces and their samples authored in a column
 * beside a viewport playing the selected space at the cursor's parameter.
 *
 * A set names a clip set and no mesh, so what it is shown on is found through the reference graph
 * (`editor::ResolveBlendSetMeshes`). What the column lists is the document rather than the rig, so a
 * set with nothing to show it on still lists and still edits; what needs the clip table -- a new space
 * or sample, thresholds from speed, the cursor -- waits for a mesh.
 *
 * The clock never wraps and never runs backwards: a space's phase is integrated from the moment it
 * was stamped, so a clock that stepped back would put the pose behind its own reference.
 */
class BlendSpaceEditorWindow :
	public QWidget,
	public editor::IHoldsAssets,
	public editor::IFollowsProject
{
	Q_OBJECT

public:
	BlendSpaceEditorWindow(
		QWidget*                     parent,
		RenderTargetWindowDesc       rt,
		editor::EnvironmentApplyDesc env);

	/**
	 * Opens the set at `key`, data-root-relative, in place of whatever was open, and shows it on the
	 * first mesh that can. A set that will not read opens listing nothing, with the reason where the
	 * viewport would be.
	 */
	void
	OpenBlendSet(const QString& key);

	/** Back to the empty state, releasing what the viewport holds. */
	void
	CloseBlendSet();

	/** The set open, data-root-relative, or empty. */
	[[nodiscard]] const QString&
	GetBlendSetKey() const noexcept
	{
		return m_BlendRelPath;
	}

	/** The open project's Data directory; closes the set, which belonged to the last one. */
	void
	SetDataRoot(const QString& dataRoot) override;

	/** Forwarded to the preview -- nullptr releases everything it holds, and closes the set. */
	void
	SetAssets(game::AssetManager* assets);

	/**
	 * Leaving the tab closes the set, as the Animation panel's `SetDockVisible` does and for its
	 * reason. MainWindow drives this through `editor::IsPanelShown`.
	 */
	void
	SetDockVisible(bool visible);

	/** The set, the clip set it names and the mesh it is shown on, absolute. */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

protected:
	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	void
	hideEvent(QHideEvent* event) override;
	void
	showEvent(QShowEvent* event) override;

private:
	[[nodiscard]] QWidget*
	BuildPropertiesColumn();

	[[nodiscard]] QWidget*
	BuildTransportBar();

	// Loads mesh `index` of the selector, playing the set's clip set with the set's spaces as nodes.
	void
	ShowMesh(int index);

	// The viewport when a mesh is on screen, and otherwise the reason there is none.
	void
	ShowViewPage();

	void
	SetClips(const std::vector<editor::ClipInfo>& clips);

	// What the rig was acquired with, and so what an edit is compared against.
	void
	SetLiveSpaces(const std::vector<game::BlendSpaceInfo>& spaces);

	// Re-lists the document's spaces, keeping the selection by name.
	void
	RefreshSpaces();

	// Lists the samples of space `index`, or the empty-state note when there is none.
	void
	SelectSpace(int index);

	// Rewrites each listed sample's threshold from the document, in place.
	void
	RelabelSamples();

	// Mirrors the selected sample into the threshold box and greys what has nothing to act on.
	void
	UpdateSpaceControls();

	/**
	 * Writes the edited document back and puts what it says on screen.
	 *
	 * Which of the two paths it takes is decided here, by `editor::IsParameterMove` against the set
	 * the rig was acquired with: a threshold that moved goes live on the uploaded rig, and anything
	 * that changed the node table reloads the mesh. With no mesh there is only the list to redraw.
	 *
	 * A refusal from the store leaves the edit in memory and on screen, where it can be corrected.
	 */
	void
	CommitBlendSet();

	// Adds a space seeded with the first two looping clips, named by the author.
	void
	AddSpace();

	void
	RemoveSpace();

	void
	RenameSpace();

	// Adds the combo's clip past the run's last sample, one whole step clear of it.
	void
	AddSample();

	void
	RemoveSample();

	// Holds the selected sample's threshold between its neighbours and pushes it live.
	void
	RetargetSample(float parameter);

	// The clips that may be sampled, non-looping ones listed and greyed with the reason.
	void
	ShowSampleClips();

	// Puts the preview on the selected space at the cursor's parameter.
	void
	ShowSelectedSpace();

	// Moves the space on screen to the cursor's parameter, and re-reads the weights under it.
	void
	MoveCursor(int tick);

	// The cursor's range and position for the selected space, and the weights beneath it.
	void
	SyncCursor();

	// Which two clips are live under the cursor and what each weighs, from `StraddleAt`.
	void
	ShowCursorWeights();

	/**
	 * Takes the selected space's thresholds from each clip's measured `locomotionSpeed`, re-sorting
	 * the run by them. Refused when two clips travel at one speed, or when one does not travel.
	 */
	void
	ThresholdsFromSpeed();

	void
	SetPlaying(bool playing);

	// One clock tick: advance by the wall time since the last, scaled, and push it into the preview.
	void
	Tick();

	[[nodiscard]] int
	LoopingClipCount() const;

	// The `n`th clip a space may sample, or -1 when there is no such clip.
	[[nodiscard]] int
	NthLoopingClip(int n) const;

	// The document's space `index`, or nullptr when there is none.
	[[nodiscard]] assetlib::BlendSpace*
	EditedSpace(int index);

	/**
	 * The rig's space `index`, or nullptr unless a mesh is on screen and the rig holds exactly the
	 * document's spaces -- the correspondence every live write rests on.
	 */
	[[nodiscard]] const game::BlendSpaceInfo*
	LiveSpace(int index) const;

	[[nodiscard]] QString
	ClipName(uint32_t clipIndex) const;

	AnimationPreviewWindow* m_Preview = nullptr;

	// The drop prompt, or the column beside the viewport.
	QStackedWidget* m_Stage = nullptr;

	// The viewport and its transport, or the note saying why there is no mesh to show.
	QStackedWidget* m_View     = nullptr;
	QLabel*         m_ViewNote = nullptr;

	QLabel* m_SetLabel = nullptr;

	// Which mesh the set is shown on. Only shown when a rig has more than one, and never saved.
	QLabel*    m_MeshCaption  = nullptr;
	QComboBox* m_MeshSelector = nullptr;

	QComboBox*   m_SpaceSelector = nullptr;
	QPushButton* m_AddSpace      = nullptr;
	QPushButton* m_RenameSpace   = nullptr;
	QPushButton* m_RemoveSpace   = nullptr;

	QListWidget*    m_SampleList      = nullptr;
	QComboBox*      m_SampleClip      = nullptr;
	QPushButton*    m_AddSample       = nullptr;
	QPushButton*    m_RemoveSample    = nullptr;
	QDoubleSpinBox* m_SampleParameter = nullptr;
	QPushButton*    m_FromSpeed       = nullptr;

	Scrubber* m_SpaceCursor  = nullptr;
	QLabel*   m_CursorLabel  = nullptr;
	QLabel*   m_SpaceWeights = nullptr;
	QLabel*   m_SpaceNote    = nullptr;

	QToolButton*    m_PlayButton = nullptr;
	QDoubleSpinBox* m_Speed      = nullptr;
	QTimer*         m_Clock      = nullptr;
	QElapsedTimer   m_ClockDelta;
	float           m_Seconds = 0.0f;
	bool            m_Playing = false;

	QString m_DataRoot;
	QString m_BlendRelPath;
	QString m_MeshRelPath;

	// Why there is no mesh on screen, when the reason is the set's rather than a failed load.
	QString m_ViewReason;

	/**
	 * The open set as authored -- clips by name -- which is what every rule takes, what is listed and
	 * what is saved. Held beside the resolved `m_Spaces` rather than derived from them, since the
	 * resolved form carries neither the document's `name` nor its `extraJson`.
	 */
	assetlib::BlendSet m_BlendSet;
	bool               m_SetReadable = false;

	// Whether a threshold has moved since the last save; the box's editingFinished fires on focus loss.
	bool m_BlendSetDirty = false;

	// The document's spaces as the rig on screen was acquired with them; empty when no rig holds them.
	std::vector<assetlib::BlendSpace> m_AcquiredSpaces;

	std::vector<game::BlendSpaceInfo> m_Spaces;
	std::vector<editor::ClipInfo>     m_Clips;

	// What the author was looking at, kept across the reload an edit causes: the space by name, and
	// the row a just-added sample lands on, spent the first time it is read.
	QString m_SelectedSpace;
	int     m_PendingSampleRow = -1;
	float   m_SpaceParameter   = 0.0f;

	bool m_SyncingUi = false;
};
