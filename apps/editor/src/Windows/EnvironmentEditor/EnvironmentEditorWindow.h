#pragma once

#include <QWidget>

#include <assetlib_structs/BEnv.h>

#include "Render/environment.h"
#include "util/held_open_assets.h"

class EnvironmentPreviewWindow;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class Renderer;

struct EnvironmentEditorWindowDesc
{
	Renderer* renderer    = nullptr;
	bool      taaEnabled  = true;
	float     renderScale = 1.0f;

	// The `.benv` the panel opens with, and the root its references resolve against -- the same
	// pair every other viewport is configured with.
	editor::EnvironmentApplyDesc startupEnv;
};

/**
 * The `.benv` editor: the document's authored state down the left, and the environment standing on
 * a subject down the right.
 *
 * What it authors is presentation and light -- the backdrop's defocus and spin, the exposure, the
 * rim. What a `.benv` *composes* (its `.bsky` and `.benvl`) is read-only here: those are what an
 * import wrote, and swapping one is a re-import rather than an edit.
 */
class EnvironmentEditorWindow : public QWidget, public editor::IHoldsAssets
{
	Q_OBJECT

public:
	explicit EnvironmentEditorWindow(
		QWidget*                    parent = nullptr,
		EnvironmentEditorWindowDesc desc   = {});
	~EnvironmentEditorWindow() override;

	void
	SetDataRoot(const QString& dataRoot);

	/** Opens the `.benv` at `path`, dropping any unsaved edit of whatever was open. */
	void
	OpenEnvironment(const QString& path);

	/** Closes whatever is open and hands back everything the viewport holds. */
	void
	Reset();

	/** The files the panel has open, absolute -- deleting one behind it would not stick. */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

private:
	/** Puts `m_Env` into every control, without any of them reading it back as an edit. */
	void
	SyncControls();

	/** Takes the controls' state into `m_Env` and shows it in the viewport. */
	void
	ApplyControls();

	void
	SaveEnvironment();

	void
	PickTint();

	EnvironmentEditorWindowDesc m_Desc;
	std::filesystem::path       m_DataRoot;

	// The document as edited, and the file it came from. An empty path is "nothing open", which is
	// what every control's enabled state follows.
	assetlib::BEnv m_Env;
	QString        m_EnvPath;
	bool           m_Dirty = false;

	// True while SyncControls is writing the controls, so their change signals do not read that
	// back as a user's edit and mark the document dirty.
	bool m_Syncing = false;

	EnvironmentPreviewWindow* m_Preview = nullptr;

	QPushButton*    m_Open             = nullptr;
	QPushButton*    m_Save             = nullptr;
	QLabel*         m_EnvironmentLabel = nullptr;
	QLabel*         m_CompositionLabel = nullptr;
	QSpinBox*       m_SkyMipLevel      = nullptr;
	QDoubleSpinBox* m_SkyRotationY     = nullptr;
	QCheckBox*      m_OverrideExposure = nullptr;
	QDoubleSpinBox* m_Exposure         = nullptr;
	QPushButton*    m_RimTint          = nullptr;
	QDoubleSpinBox* m_RimIntensity     = nullptr;
	QDoubleSpinBox* m_RimPower         = nullptr;
};
