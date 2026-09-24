#pragma once

#include <QString>
#include <bgl/SurfaceType.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <qnamespace.h>
#include <span>
#include <vector>

class MaterialGraphView;
class SurfaceOutputNode;
class QCheckBox;
class QComboBox;
class QListWidget;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QWidget;

namespace editor
{
	/** Set on the Material list's default row, for the delegate that marks it. */
	constexpr int c_IsDefaultMaterialRole = Qt::UserRole + 1;

	/** One Output selector entry: the label it shows, and the registered sink choosing it swaps
	 *  in. */
	struct OutputType
	{
		QString label;
		QString modelName;
	};

	/**
	 * The Output selector's entries, in the order it lists them: the four PBR sinks -- choosing
	 * one *is* choosing the alpha mode -- then one entry per registered surface, labelled with
	 * the surface's name. Built beside the registry from the same surface list, so an entry
	 * always names a sink that exists.
	 */
	[[nodiscard]] std::vector<OutputType>
	OutputTypesFor(const ILanguageResolver& language, std::span<const bgl::SurfaceType> surfaces);

	/**
	 * The widgets BuildMaterialEditorUi creates, so the window can connect and drive them.
	 *
	 * `leftPanel` is the half of the editor this builds -- the properties column beside the graph
	 * board. What goes on the right is the preview, which needs a graphics device and so is the
	 * window's to decide.
	 */
	struct MaterialEditorWidgets
	{
		QWidget*           leftPanel        = nullptr;
		MaterialGraphView* graphView        = nullptr;
		QPushButton*       open             = nullptr;
		QPushButton*       bakeAll          = nullptr;
		QPushButton*       addOverride      = nullptr;
		QPushButton*       removeOverride   = nullptr;
		QListWidget*       materialList     = nullptr;
		QPushButton*       generateTangents = nullptr;
		QComboBox*         submeshSelector  = nullptr;
		QComboBox*         outputSelector   = nullptr;
		QLabel*            tangentWarning   = nullptr;

		// The surface layer (ADR-9), edited here rather than on the node; FillLayerSection shows,
		// hides and fills it.
		QWidget*        layerSection  = nullptr;
		QFormLayout*    layerForm     = nullptr;
		QComboBox*      layerSelector = nullptr;
		QDoubleSpinBox* alphaCutoff   = nullptr;
		QCheckBox*      doubleSided   = nullptr;
	};

	/**
	 * Shows the Layer section and fills it from `sink`, or hides it for null -- a PBR board,
	 * whose layer is the Output selector's sink choice. The cutoff row shows on a mask layer
	 * alone. Signal-blocked, so a fill never writes back through the window's connects.
	 */
	void
	FillLayerSection(const SurfaceOutputNode* sink, const MaterialEditorWidgets& widgets);

	/**
	 * Builds the material editor's properties column and graph board under `parent`, in the state they
	 * start in: every action disabled, every conditional label hidden.
	 *
	 * Nothing is connected -- what each widget *does* is the window's, and keeping the two apart is
	 * what lets this be read as a layout rather than as behaviour.
	 *
	 * `language` must outlive the widgets it builds.
	 */
	[[nodiscard]] MaterialEditorWidgets
	BuildMaterialEditorUi(const ILanguageResolver& language, QWidget* parent);
}
