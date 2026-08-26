#include "material_editor_ui.h"

#include "Windows/MaterialEditor/MaterialGraphView.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace editor
{
	MaterialEditorWidgets
	BuildMaterialEditorUi(QWidget* parent)
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

		// Material file actions, acting on the selected submesh's graph.
		widgets.open   = new QPushButton(QStringLiteral("Open..."), propertiesPanel);
		widgets.save   = new QPushButton(QStringLiteral("Save"), propertiesPanel);
		widgets.saveAs = new QPushButton(QStringLiteral("Save As..."), propertiesPanel);

		auto* fileActions = new QHBoxLayout();
		fileActions->setContentsMargins(0, 0, 0, 0);
		fileActions->addWidget(widgets.open);
		fileActions->addWidget(widgets.save);
		fileActions->addWidget(widgets.saveAs);
		propertiesLayout->addLayout(fileActions);

		// The whole mesh rather than the selected submesh: a mesh with a dozen submeshes is a dozen
		// trips through the selector otherwise. Submeshes wearing one material share a graph, so the
		// file is written once however many of them name it.
		widgets.saveAll = new QPushButton(QStringLiteral("Save All"), propertiesPanel);
		widgets.saveAll->setToolTip(QStringLiteral(
			"Write every material of this mesh.\nA submesh whose graph has no file yet is skipped "
			"-- "
			"Save As gives it one."));

		widgets.bakeAll = new QPushButton(QStringLiteral("Bake All"), propertiesPanel);
		widgets.bakeAll->setToolTip(QStringLiteral(
			"Save every material of this mesh, then composite each down to its baked "
			"textures.\nA bake reads the routes off disk, so it saves first."));

		auto* meshActions = new QHBoxLayout();
		meshActions->setContentsMargins(0, 0, 0, 0);
		meshActions->addWidget(widgets.saveAll);
		meshActions->addWidget(widgets.bakeAll);
		propertiesLayout->addLayout(meshActions);

		widgets.setDefault =
			new QPushButton(QStringLiteral("Set Default Material"), propertiesPanel);
		widgets.setDefault->setToolTip(QStringLiteral(
			"Bind this material to the submesh in the .bmesh, so every instance of the mesh loads "
			"with it.\nThe preview only overrides the instances in front of you until you do."));
		propertiesLayout->addWidget(widgets.setDefault);

		// The path of the `.bmaterial` the selected submesh is bound to, so it is clear what Save writes
		// to.
		widgets.materialLabel = new QLabel(propertiesPanel);
		widgets.materialLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		widgets.materialLabel->setWordWrap(true);
		widgets.materialLabel->setStyleSheet("color: gray;");

		// A path has no spaces to wrap at, so the label's minimum width would otherwise be a whole
		// directory name and become the floor for the panel -- and for the splitter above it. Ignored
		// drops it out of that calculation; the tooltip carries the path once it is too narrow to read.
		widgets.materialLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		propertiesLayout->addWidget(widgets.materialLabel);

		propertiesLayout->addSpacing(8);

		propertiesLayout->addWidget(new QLabel(QStringLiteral("Submesh"), propertiesPanel));
		widgets.submeshSelector = new QComboBox(propertiesPanel);
		widgets.submeshSelector->setPlaceholderText("No submesh");
		widgets.submeshSelector->setEnabled(false);
		propertiesLayout->addWidget(widgets.submeshSelector);

		// The graph's sink, chosen rather than dragged in: a material has exactly one, and which one it is
		// *is* the alpha mode. The context menu does not offer them (see MaterialGraphScene).
		propertiesLayout->addWidget(new QLabel(QStringLiteral("Output"), propertiesPanel));
		widgets.outputSelector = new QComboBox(propertiesPanel);
		for (const OutputType& type : c_OutputTypes)
			widgets.outputSelector->addItem(QLatin1String(type.label));
		widgets.outputSelector->setEnabled(false);
		widgets.outputSelector->setToolTip(QStringLiteral(
			"Alpha Tested adds a base-color alpha input and a cutoff: pixels below it are "
			"discarded. Alpha Blend uses that alpha to blend the surface, back-to-front, with no "
			"cutoff."));
		propertiesLayout->addWidget(widgets.outputSelector);

		// The material's current baked textures, if any. Read-only: the graph authors the routes they are
		// composited from, and Bake All above -- or the Content Explorer's Bake -- is what rewrites them.
		widgets.bakedTextures = new QLabel(propertiesPanel);
		widgets.bakedTextures->setTextInteractionFlags(Qt::TextSelectableByMouse);
		widgets.bakedTextures->setWordWrap(true);
		widgets.bakedTextures->setStyleSheet("color: gray;");
		widgets.bakedTextures->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		widgets.bakedTextures->hide();
		propertiesLayout->addWidget(widgets.bakedTextures);

		// A normal map routed onto a submesh with no tangent renders as nothing: the shader rebuilds the
		// map's frame from the tangent and falls back to the geometric normal without one. Silent until
		// this said so.
		widgets.tangentWarning = new QLabel(propertiesPanel);
		widgets.tangentWarning->setWordWrap(true);
		widgets.tangentWarning->setStyleSheet("color: #d08770;");
		widgets.tangentWarning->setText(
			QStringLiteral("This submesh has no tangents, so its normal map is being ignored."));
		widgets.tangentWarning->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		widgets.tangentWarning->hide();
		propertiesLayout->addWidget(widgets.tangentWarning);

		widgets.generateTangents =
			new QPushButton(QStringLiteral("Generate Tangents"), propertiesPanel);
		widgets.generateTangents->setToolTip(QStringLiteral(
			"Derive a tangent for every submesh of this mesh that has none, and rewrite the "
			".bmesh."));
		widgets.generateTangents->hide();
		propertiesLayout->addWidget(widgets.generateTangents);

		// A property of the mesh, not of the material: how much of the environment's rim a surface
		// catches is a decision about the object, and a material shared by a unit and a wall would
		// have to answer for both. Zero by default -- see docs/envmaps.md.
		auto* rimRow    = new QWidget(propertiesPanel);
		auto* rimLayout = new QHBoxLayout(rimRow);
		rimLayout->setContentsMargins(0, 0, 0, 0);
		rimLayout->addWidget(new QLabel(QStringLiteral("Rim lighting (whole mesh)"), rimRow));

		widgets.rimIntensity = new QDoubleSpinBox(rimRow);
		widgets.rimIntensity->setObjectName(QStringLiteral("MeshRimIntensity"));
		widgets.rimIntensity->setRange(0.0, 16.0);
		widgets.rimIntensity->setSingleStep(0.1);
		widgets.rimIntensity->setDecimals(2);
		widgets.rimIntensity->setEnabled(false);
		widgets.rimIntensity->setToolTip(QStringLiteral(
			"How much of the environment's rim light the mesh this submesh belongs to catches, in "
			"every scene it is placed in. Zero catches none.\nAuthored in the mesh's .bimport, so "
			"a re-import keeps it. The environment decides the rim's colour and strength; an "
			"environment with none shows nothing here."));
		rimLayout->addWidget(widgets.rimIntensity);
		propertiesLayout->addWidget(rimRow);

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
