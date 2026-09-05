#pragma once

#include <QString>
#include <array>

class MaterialGraphView;
class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

namespace editor
{
	/** A sink a material graph can end in, and the alpha mode choosing it *is*. */
	struct OutputType
	{
		const char* label;
		const char* modelName;
	};

	/** The sinks the Output selector offers, in the order it lists them. */
	inline constexpr std::array<OutputType, 4> c_OutputTypes = { {
		{ "Opaque", "MaterialOutput" },
		{ "Alpha Tested", "AlphaTestedMaterialOutput" },
		{ "Alpha Blend", "BlendedMaterialOutput" },
		{ "Hashed Alpha", "HashedAlphaMaterialOutput" },
	} };

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
