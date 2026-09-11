#pragma once

#include <QString>
#include <bgl/SurfaceType.h>
#include <span>
#include <vector>

class MaterialGraphView;
class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

namespace editor
{
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
	OutputTypesFor(std::span<const bgl::SurfaceType> surfaces);

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
		QPushButton*       save             = nullptr;
		QPushButton*       saveAs           = nullptr;
		QPushButton*       saveAll          = nullptr;
		QPushButton*       bakeAll          = nullptr;
		QPushButton*       setDefault       = nullptr;
		QPushButton*       generateTangents = nullptr;
		QComboBox*         submeshSelector  = nullptr;
		QComboBox*         outputSelector   = nullptr;
		QLabel*            materialLabel    = nullptr;
		QLabel*            bakedTextures    = nullptr;
		QLabel*            tangentWarning   = nullptr;
	};

	/**
	 * Builds the material editor's properties column and graph board under `parent`, in the state they
	 * start in: every action disabled, every conditional label hidden.
	 *
	 * Nothing is connected -- what each widget *does* is the window's, and keeping the two apart is
	 * what lets this be read as a layout rather than as behaviour.
	 */
	[[nodiscard]] MaterialEditorWidgets
	BuildMaterialEditorUi(QWidget* parent);
}
