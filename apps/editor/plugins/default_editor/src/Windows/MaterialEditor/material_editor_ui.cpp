#include "material_editor_ui.h"

#include "Windows/MaterialEditor/MaterialGraphView.h"
#include "Windows/MaterialEditor/nodes/SurfaceOutputNode.h"
#include <editor_plugin_api/ILanguageResolver.h>

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QModelIndex>
#include <QObject>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <cstddef>
#include <editor_plugin_api/localize.h>
#include <qnamespace.h>
#include <qsizepolicy.h>
#include <qstring.h>
#include <qstringliteral.h>
#include <span>
#include <vector>

namespace
{
	// Enough of a mesh's looks to read at a glance; the rest scroll.
	constexpr int c_MaterialListHeight = 132;

	// A strip under the list, not a row of buttons: square, and small enough to read as part of it.
	constexpr int c_ListButtonSize = 22;

	constexpr int   c_TagGap  = 6;
	constexpr float c_TagFade = 0.7f;

	/** Draws the default look's row: its name bold, and a grey `default` tag against the margin. */
	class DefaultTagDelegate : public QStyledItemDelegate
	{
	public:
		DefaultTagDelegate(const editor::ILanguageResolver& language, QObject* parent) :
			QStyledItemDelegate(parent), m_Language(language)
		{}

		void
		paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index)
			const override
		{
			if (!index.data(editor::c_IsDefaultMaterialRole).toBool())
			{
				QStyledItemDelegate::paint(painter, option, index);
				return;
			}

			QStyleOptionViewItem row = option;
			initStyleOption(&row, index);
			row.font.setBold(true);

			// The text is drawn below, in two pieces; the base would draw the model's own over it.
			const QString name = row.text;
			row.text.clear();

			const QWidget* widget = row.widget;
			QStyle*        style  = widget != nullptr ? widget->style() : QApplication::style();
			style->drawControl(QStyle::CE_ItemViewItem, &row, painter, widget);

			const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &row, widget);
			const QFontMetrics metrics(row.font);
			const QString      tag =
				editor::Localize(m_Language, "bernini.material.default_look_tag", "default");
			const int span = metrics.horizontalAdvance(tag) + c_TagGap;

			const bool selected = row.state.testFlag(QStyle::State_Selected);

			painter->save();
			painter->setFont(row.font);

			// On the selection the grey would read as unreadable rather than as secondary, so the
			// tag takes the highlight's own text colour, softened.
			QColor tagColor = row.palette.color(
				selected ? QPalette::Normal : QPalette::Disabled,
				selected ? QPalette::HighlightedText : QPalette::Text);
			if (selected)
				tagColor.setAlphaF(c_TagFade);

			painter->setPen(tagColor);
			painter->drawText(text, Qt::AlignRight | Qt::AlignVCenter, tag);

			painter->setPen(
				row.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
			painter->drawText(
				text.adjusted(0, 0, -span, 0),
				Qt::AlignLeft | Qt::AlignVCenter,
				metrics.elidedText(name, Qt::ElideMiddle, text.width() - span));
			painter->restore();
		}

	private:
		const editor::ILanguageResolver& m_Language;
	};

	// Indexed by assetlib::AlphaMode, both for the Layer combo and for the Output selector's four
	// PBR entries -- the same four words either way.
	QString
	LayerLabelOf(const editor::ILanguageResolver& language, size_t alphaMode)
	{
		switch (static_cast<assetlib::AlphaMode>(alphaMode))
		{
		case assetlib::AlphaMode::kOpaque:
			return editor::Localize(language, "bernini.material.layer_label_opaque", "Opaque");
		case assetlib::AlphaMode::kMask:
			return editor::Localize(
				language,
				"bernini.material.layer_label_alpha_tested",
				"Alpha Tested");
		case assetlib::AlphaMode::kBlend:
			return editor::Localize(
				language,
				"bernini.material.layer_label_alpha_blend",
				"Alpha Blend");
		case assetlib::AlphaMode::kHashed:
			return editor::Localize(
				language,
				"bernini.material.layer_label_hashed_alpha",
				"Hashed Alpha");
		}
		return {};
	}
}

namespace editor
{
	std::vector<OutputType>
	OutputTypesFor(const ILanguageResolver& language, std::span<const bgl::SurfaceType> surfaces)
	{
		auto types = std::vector<OutputType>{
			{ LayerLabelOf(language, static_cast<size_t>(assetlib::AlphaMode::kOpaque)),
			  QStringLiteral("MaterialOutput") },
			{ LayerLabelOf(language, static_cast<size_t>(assetlib::AlphaMode::kMask)),
			  QStringLiteral("AlphaTestedMaterialOutput") },
			{ LayerLabelOf(language, static_cast<size_t>(assetlib::AlphaMode::kBlend)),
			  QStringLiteral("BlendedMaterialOutput") },
			{ LayerLabelOf(language, static_cast<size_t>(assetlib::AlphaMode::kHashed)),
			  QStringLiteral("HashedAlphaMaterialOutput") },
		};

		types.reserve(types.size() + surfaces.size());
		for (const bgl::SurfaceType& surface : surfaces)
		{
			types.emplace_back(
				QString::fromStdString(surface.name),
				SurfaceOutputNode::ModelNameFor(surface.name));
		}

		return types;
	}

	void
	FillLayerSection(const SurfaceOutputNode* sink, const MaterialEditorWidgets& widgets)
	{
		widgets.layerSection->setVisible(sink != nullptr);
		if (sink == nullptr)
			return;

		{
			const QSignalBlocker blocker(widgets.layerSelector);
			widgets.layerSelector->setCurrentIndex(static_cast<int>(sink->GetAlphaMode()));
		}
		{
			const QSignalBlocker blocker(widgets.alphaCutoff);
			widgets.alphaCutoff->setValue(static_cast<double>(sink->GetAlphaCutoff()));
		}
		{
			const QSignalBlocker blocker(widgets.doubleSided);
			widgets.doubleSided->setChecked(sink->GetDoubleSided());
		}

		// The cutoff is read on a mask layer alone -- hashed replaces it with stochastic
		// coverage.
		widgets.layerForm->setRowVisible(
			widgets.alphaCutoff,
			sink->GetAlphaMode() == assetlib::AlphaMode::kMask);
	}

	MaterialEditorWidgets
	BuildMaterialEditorUi(const ILanguageResolver& language, QWidget* parent)
	{
		auto widgets = MaterialEditorWidgets();

		widgets.leftPanel = new QWidget(parent);

		auto* leftLayout = new QVBoxLayout(widgets.leftPanel);
		leftLayout->setContentsMargins(0, 0, 0, 0);
		leftLayout->setSpacing(0);

		// A material properties panel down the left of the graph: what the material *is* -- the file it is
		// bound to, its actions, which submesh, the output type, and its baked textures -- kept apart from
		// the board that wires it.
		auto* graphSplitter = new QSplitter(Qt::Horizontal, widgets.leftPanel);

		auto* propertiesPanel  = new QWidget(graphSplitter);
		auto* propertiesLayout = new QVBoxLayout(propertiesPanel);
		propertiesLayout->setContentsMargins(4, 4, 4, 4);

		// The panel writes by itself, so there is nothing here to save with: what is left is
		// putting an existing material on the board, and baking what the graphs route.
		widgets.open = new QPushButton(
			editor::Localize(language, "bernini.material.open_button", "Open..."),
			propertiesPanel);

		widgets.bakeAll = new QPushButton(
			editor::Localize(language, "bernini.material.bake_all_button", "Bake All"),
			propertiesPanel);
		widgets.bakeAll->setToolTip(
			editor::Localize(
				language,
				"bernini.material.bake_all_tooltip",
				"Composite every material of this mesh down to its baked textures.\nA bake reads "
				"the "
				"routes off disk, so it writes what is still pending first."));

		auto* fileActions = new QHBoxLayout();
		fileActions->setContentsMargins(0, 0, 0, 0);
		fileActions->addWidget(widgets.open);
		fileActions->addWidget(widgets.bakeAll);
		propertiesLayout->addLayout(fileActions);

		// Every look this submesh can wear: its default, then the overrides the mesh registers. A
		// game reaches one of these by name (AssetManager::SetInstanceSubmeshMaterialOverride), so
		// the list is the asset's own, not the panel's. A list rather than a drop-down, for the
		// reason the Animation editor lists clips: it is a place you work, not a setting you pick.
		propertiesLayout->addWidget(new QLabel(
			editor::Localize(language, "bernini.material.material_list_label", "Material"),
			propertiesPanel));

		widgets.materialList = new QListWidget(propertiesPanel);
		widgets.materialList->setEnabled(false);
		widgets.materialList->setItemDelegate(
			new DefaultTagDelegate(language, widgets.materialList));
		widgets.materialList->setContextMenuPolicy(Qt::CustomContextMenu);
		widgets.materialList->setMaximumHeight(c_MaterialListHeight);
		widgets.materialList->setToolTip(
			editor::Localize(
				language,
				"bernini.material.material_list_tooltip",
				"The looks this submesh can wear. Click one to edit and preview it; double-click "
				"to "
				"make it the mesh's default.\nRight-click for the rest."));
		propertiesLayout->addWidget(widgets.materialList);

		// Under the list rather than beside it, and flat: registering a look is an edit to the
		// list, not one of the panel's actions.
		widgets.addOverride = new QPushButton(
			editor::Localize(language, "bernini.material.add_override_glyph", "+"),
			propertiesPanel);
		widgets.removeOverride = new QPushButton(
			editor::Localize(language, "bernini.material.remove_override_glyph", "−"),
			propertiesPanel);
		for (QPushButton* button : { widgets.addOverride, widgets.removeOverride })
		{
			button->setFlat(true);
			button->setFixedSize(c_ListButtonSize, c_ListButtonSize);
		}

		auto* listActions = new QHBoxLayout();
		listActions->setContentsMargins(0, 0, 0, 0);
		listActions->setSpacing(0);
		listActions->addWidget(widgets.addOverride);
		listActions->addWidget(widgets.removeOverride);
		listActions->addStretch(1);
		propertiesLayout->addLayout(listActions);

		propertiesLayout->addSpacing(8);

		propertiesLayout->addWidget(new QLabel(
			editor::Localize(language, "bernini.material.submesh_label", "Submesh"),
			propertiesPanel));
		widgets.submeshSelector = new QComboBox(propertiesPanel);
		widgets.submeshSelector->setPlaceholderText(
			editor::Localize(language, "bernini.material.no_submesh_placeholder", "No submesh"));
		widgets.submeshSelector->setEnabled(false);
		propertiesLayout->addWidget(widgets.submeshSelector);

		// The graph's sink, chosen rather than dragged in: a material has exactly one, and which one it is
		// *is* the alpha mode. The context menu does not offer them (see MaterialGraphScene).
		propertiesLayout->addWidget(new QLabel(
			editor::Localize(language, "bernini.material.output_label", "Output"),
			propertiesPanel));
		// Entries arrive from the window (OutputTypesFor): the surfaces half is known only once
		// the registry is.
		widgets.outputSelector = new QComboBox(propertiesPanel);
		widgets.outputSelector->setEnabled(false);
		widgets.outputSelector->setToolTip(
			editor::Localize(
				language,
				"bernini.material.output_selector_tooltip",
				"Alpha Tested adds a base-color alpha input and a cutoff: pixels below it are "
				"discarded. Alpha Blend uses that alpha to blend the surface, back-to-front, with "
				"no "
				"cutoff. A surface entry hands the material to that game surface, whose parameters "
				"appear on its output node."));
		propertiesLayout->addWidget(widgets.outputSelector);

		// The surface layer (ADR-9), filled by FillLayerSection below.
		widgets.layerSection = new QWidget(propertiesPanel);
		widgets.layerForm    = new QFormLayout(widgets.layerSection);
		widgets.layerForm->setContentsMargins(0, 0, 0, 0);

		widgets.layerSelector = new QComboBox(widgets.layerSection);
		for (size_t mode = 0; mode <= static_cast<size_t>(assetlib::AlphaMode::kHashed); ++mode)
			widgets.layerSelector->addItem(LayerLabelOf(language, mode));
		widgets.layerSelector->setToolTip(
			editor::Localize(
				language,
				"bernini.material.layer_selector_tooltip",
				"How this surface material's alpha is read: discarded below a cutoff (Alpha "
				"Tested), "
				"blended back-to-front (Alpha Blend), or stochastic coverage under temporal AA "
				"(Hashed Alpha)."));
		widgets.layerForm->addRow(
			editor::Localize(language, "bernini.material.layer_row_label", "Layer"),
			widgets.layerSelector);

		widgets.alphaCutoff = new QDoubleSpinBox(widgets.layerSection);
		widgets.alphaCutoff->setRange(0.0, 1.0);
		widgets.alphaCutoff->setSingleStep(0.05);
		widgets.alphaCutoff->setDecimals(3);

		// Commit, not keystroke: per-keystroke writes reach the sink, whose Changed re-fills this
		// very box and rewrites the text under the user's caret -- and recompile the preview once
		// per digit besides.
		widgets.alphaCutoff->setKeyboardTracking(false);
		widgets.layerForm->addRow(
			editor::Localize(language, "bernini.material.alpha_cutoff_row_label", "Alpha Cutoff"),
			widgets.alphaCutoff);

		widgets.doubleSided = new QCheckBox(widgets.layerSection);
		widgets.layerForm->addRow(
			editor::Localize(language, "bernini.material.double_sided_row_label", "Double Sided"),
			widgets.doubleSided);

		widgets.layerSection->hide();
		propertiesLayout->addWidget(widgets.layerSection);

		// A normal map routed onto a submesh with no tangent renders as nothing: the shader rebuilds the
		// map's frame from the tangent and falls back to the geometric normal without one. Silent until
		// this said so.
		widgets.tangentWarning = new QLabel(propertiesPanel);
		widgets.tangentWarning->setWordWrap(true);
		widgets.tangentWarning->setStyleSheet("color: #d08770;");
		widgets.tangentWarning->setText(
			editor::Localize(
				language,
				"bernini.material.tangent_warning",
				"This submesh has no tangents, so its normal map is being ignored."));
		widgets.tangentWarning->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		widgets.tangentWarning->hide();
		propertiesLayout->addWidget(widgets.tangentWarning);

		widgets.generateTangents = new QPushButton(
			editor::Localize(
				language,
				"bernini.material.generate_tangents_button",
				"Generate Tangents"),
			propertiesPanel);
		widgets.generateTangents->setToolTip(
			editor::Localize(
				language,
				"bernini.material.generate_tangents_tooltip",
				"Derive a tangent for every submesh of this mesh that has none, and rewrite the "
				".bmesh."));
		widgets.generateTangents->hide();
		propertiesLayout->addWidget(widgets.generateTangents);

		propertiesLayout->addStretch(1);

		widgets.graphView = new MaterialGraphView(graphSplitter);  // scene set per selected submesh

		// The properties panel keeps its width; the graph takes the rest.
		graphSplitter->addWidget(propertiesPanel);
		graphSplitter->addWidget(widgets.graphView);
		graphSplitter->setStretchFactor(0, 0);
		graphSplitter->setStretchFactor(1, 1);
		graphSplitter->setSizes({ 250, 800 });

		leftLayout->addWidget(graphSplitter);

		return widgets;
	}
}
