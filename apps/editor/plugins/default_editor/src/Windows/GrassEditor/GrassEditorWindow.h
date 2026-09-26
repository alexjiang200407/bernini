#pragma once
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/OrbitCamera.h>
#include <editor_sdk/environment.h>

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <assetlib/grass_patch.h>
#include <assetlib_structs/BGrass.h>
#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/types/WindDesc.h>
#include <core/glm.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <qnamespace.h>
#include <qtmetamacros.h>
#include <string>
#include <string_view>
#include <vector>

class QDoubleSpinBox;
class QEvent;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace editor
{
	/** The patch a preview grows `look` on: twice its fade end across, so it thins out before its edge. */
	[[nodiscard]] assetlib::GrassPatchDesc
	PreviewPatchFor(const assetlib::BGrass& look) noexcept;

	/** The preview's wind from what its strip shows: `heading` in degrees clockwise from +X seen from above. */
	[[nodiscard]] bgl::WindDesc
	PreviewWind(
		float strength,
		float heading,
		float gustStrength,
		float gustScale,
		float gustSpeed) noexcept;
}

/**
 * The Grass Editor: one `.bgrass` open, every value of the look in a column beside a patch of it
 * growing in the wind.
 *
 * An edit is drawn at once and written only on Save, or when the panel is closed with it pending:
 * a value dragged through its range redraws the look in place (`AssetManager::SetGrassLook`) rather
 * than writing a file and rebuilding the patch per step. Revert puts back what the file says.
 *
 * The wind is the preview's and is never saved: a look says how it answers the wind, a view says
 * what the wind is.
 */
class GrassEditorWindow : public editor::AssetEditorPanel
{
	Q_OBJECT

public:
	GrassEditorWindow(
		editor::IEditorHost&         host,
		QWidget*                     parent,
		editor::ViewportDesc         rt,
		editor::EnvironmentApplyDesc env);
	~GrassEditorWindow() override;

	/**
	 * Opens the look at `key` in place of the one open, committing a pending edit to that one first.
	 * A look that will not read, or a pending edit that will not write, leaves the open one as it was.
	 */
	void
	OpenAsset(std::string_view key) override;

	/** Writes the edited look to its file. A refusal keeps the edit, and says why in the status line. */
	bool
	Save();

	/** Puts back what the file says, on screen and in the preview. */
	void
	Revert();

	/** The look open, a mount key, or empty. */
	[[nodiscard]] const std::string&
	GetKey() const noexcept
	{
		return m_Key;
	}

	/** The look as edited, which is what Save writes. */
	[[nodiscard]] const assetlib::BGrass&
	GetLook() const noexcept
	{
		return m_Look;
	}

	/** What the preview draws: the last edit it accepted, or nullopt while it draws no grass. */
	[[nodiscard]] const std::optional<assetlib::BGrass>&
	GetDrawnLook() const noexcept
	{
		return m_Drawn;
	}

	[[nodiscard]] bool
	IsDirty() const noexcept
	{
		return m_Look != m_Saved;
	}

	std::vector<std::string>
	GetHeldAssets() const override;
	bool
	CanClose() override;
	void
	SetActive(bool active) override;
	void
	OnAssetChanged(std::string_view key) override;

protected:
	bool
	eventFilter(QObject* watched, QEvent* event) override;

private:
	[[nodiscard]] QWidget*
	BuildColumn();

	[[nodiscard]] QWidget*
	BuildWindStrip();

	void
	AddReal(
		QFormLayout*                                    form,
		const char*                                     key,
		const QString&                                  label,
		double                                          min,
		double                                          max,
		double                                          step,
		const std::function<float&(assetlib::BGrass&)>& value,
		bool                                            resizesPatch = false);

	void
	AddCount(
		QFormLayout*                                       form,
		const char*                                        key,
		const QString&                                     label,
		int                                                min,
		int                                                max,
		const std::function<uint32_t&(assetlib::BGrass&)>& value);

	void
	AddColour(
		QFormLayout*                                        form,
		const char*                                         key,
		const QString&                                      label,
		const std::function<glm::vec3&(assetlib::BGrass&)>& value);

	// Every widget from the document, without any of them reporting an edit back.
	void
	SyncFields();

	// The document changed: redraw it, and mark the title.
	void
	Edited(bool resizesPatch);

	// Puts the edited look on the drawn one in place, or grows the patch again when it cannot be.
	void
	PushLook();

	// Grows the patch for the edited look, dropping whatever was drawn before.
	void
	BuildPreview();

	void
	ReleasePreview();

	void
	SetStatus(const QString& text);

	void
	UpdateTitle();

	void
	UpdateCamera();

	void
	PushWind();

	void
	Tick();

	editor::IEditorHost&     m_Host;
	editor::IEditorViewport* m_Viewport = nullptr;

	// The prompt with nothing open, or the column beside the viewport.
	QStackedWidget* m_Stage = nullptr;

	QLabel*      m_Title    = nullptr;
	QLabel*      m_Status   = nullptr;
	QLineEdit*   m_Material = nullptr;
	QPushButton* m_Save     = nullptr;
	QPushButton* m_Revert   = nullptr;

	// Each puts one value of the look back on its widget.
	std::vector<std::function<void()>> m_Syncs;

	QDoubleSpinBox* m_WindStrength     = nullptr;
	QDoubleSpinBox* m_WindHeading      = nullptr;
	QDoubleSpinBox* m_WindGustStrength = nullptr;
	QDoubleSpinBox* m_WindGustScale    = nullptr;
	QDoubleSpinBox* m_WindGustSpeed    = nullptr;

	QTimer*       m_Clock = nullptr;
	QElapsedTimer m_Elapsed;

	editor::OrbitCamera m_Orbit;
	Qt::MouseButton     m_DragButton = Qt::NoButton;
	QPoint              m_LastMousePos;

	editor::EnvironmentBinding m_Environment;
	bgl::MaterialHandle        m_Ground;
	bgl::GeomHandle            m_Patch;
	bgl::MeshInstanceHandle    m_Instance;

	std::string                     m_Key;
	assetlib::BGrass                m_Look;
	assetlib::BGrass                m_Saved;
	std::optional<assetlib::BGrass> m_Drawn;

	bool m_Syncing = false;
};
