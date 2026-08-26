#pragma once

#include <bgl/glm.h>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QWidget;

namespace editor
{
	/**
	 * The widgets BuildEnvironmentEditorUi creates, so the window can connect and drive them.
	 *
	 * `panel` is the half of the editor this builds -- the properties column. What goes beside it is
	 * the preview, which needs a graphics device and so is the window's to decide.
	 */
	struct EnvironmentEditorWidgets
	{
		QWidget* panel = nullptr;

		QPushButton* open = nullptr;
		QPushButton* save = nullptr;

		QLabel* environmentLabel = nullptr;
		QLabel* compositionLabel = nullptr;

		QSpinBox*       skyMipLevel  = nullptr;
		QDoubleSpinBox* skyRotationY = nullptr;

		QCheckBox*      overrideExposure = nullptr;
		QDoubleSpinBox* exposure         = nullptr;

		QPushButton*    rimTint      = nullptr;
		QDoubleSpinBox* rimIntensity = nullptr;
		QDoubleSpinBox* rimPower     = nullptr;
	};

	/**
	 * Builds the environment editor's properties column under `parent`, in the state it starts in:
	 * every control disabled, because nothing is open.
	 *
	 * Nothing is connected -- what each widget *does* is the window's, and keeping the two apart is
	 * what lets this be read as a layout rather than as behaviour.
	 */
	[[nodiscard]] EnvironmentEditorWidgets
	BuildEnvironmentEditorUi(QWidget* parent);

	/** Paints `button`'s swatch with `tint` and puts the same colour in its text. */
	void
	SetTintSwatch(QPushButton* button, const glm::vec3& tint);
}
