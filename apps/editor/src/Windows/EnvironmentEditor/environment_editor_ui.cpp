#include "environment_editor_ui.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace editor
{
	namespace
	{
		QLabel*
		Heading(const QString& text, QWidget* parent)
		{
			auto* label = new QLabel(text, parent);
			label->setStyleSheet("font-weight: bold;");
			return label;
		}

		/** A grey, wrapping, width-ignoring label -- the shape every read-only line here takes. */
		QLabel*
		Caption(QWidget* parent)
		{
			auto* label = new QLabel(parent);
			label->setTextInteractionFlags(Qt::TextSelectableByMouse);
			label->setWordWrap(true);
			label->setStyleSheet("color: gray;");

			// A path has no spaces to wrap at, so its minimum width would otherwise become the
			// floor for the whole column.
			label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
			return label;
		}
	}

	void
	SetTintSwatch(QPushButton* button, const glm::vec3& tint)
	{
		const auto channel = [](float v) {
			return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
		};

		const QColor colour(channel(tint.r), channel(tint.g), channel(tint.b));

		button->setText(colour.name(QColor::HexRgb).toUpper());
		button->setStyleSheet(
			QStringLiteral("background-color: %1; color: %2;")
				.arg(colour.name(QColor::HexRgb))
				.arg(
					colour.lightnessF() > 0.5 ? QStringLiteral("black") : QStringLiteral("white")));
	}

	EnvironmentEditorWidgets
	BuildEnvironmentEditorUi(QWidget* parent)
	{
		auto widgets = EnvironmentEditorWidgets();

		widgets.panel = new QWidget(parent);

		auto* layout = new QVBoxLayout(widgets.panel);
		layout->setContentsMargins(8, 8, 8, 8);

		widgets.open = new QPushButton(QStringLiteral("Open..."), widgets.panel);
		widgets.save = new QPushButton(QStringLiteral("Save"), widgets.panel);
		widgets.save->setEnabled(false);

		auto* fileActions = new QHBoxLayout();
		fileActions->setContentsMargins(0, 0, 0, 0);
		fileActions->addWidget(widgets.open);
		fileActions->addWidget(widgets.save);
		layout->addLayout(fileActions);

		widgets.environmentLabel = Caption(widgets.panel);
		widgets.environmentLabel->setText(QStringLiteral("(no environment open)"));
		layout->addWidget(widgets.environmentLabel);

		// The sky and the lighting are read-only here: they are what an import wrote, and swapping
		// one is a re-import rather than an edit. What this panel authors is presentation.
		widgets.compositionLabel = Caption(widgets.panel);
		layout->addWidget(widgets.compositionLabel);

		layout->addSpacing(8);
		layout->addWidget(Heading(QStringLiteral("Backdrop"), widgets.panel));

		layout->addWidget(new QLabel(QStringLiteral("Sky mip level"), widgets.panel));
		widgets.skyMipLevel = new QSpinBox(widgets.panel);
		widgets.skyMipLevel->setRange(0, 15);
		widgets.skyMipLevel->setToolTip(QStringLiteral(
			"Which level of the sky's chain the backdrop samples. Above 0 defocuses it, reading as "
			"depth of field.\nClamped at load to the mips the baked map actually has."));
		layout->addWidget(widgets.skyMipLevel);

		layout->addWidget(new QLabel(QStringLiteral("Sky rotation (degrees)"), widgets.panel));
		widgets.skyRotationY = new QDoubleSpinBox(widgets.panel);
		widgets.skyRotationY->setRange(-360.0, 360.0);
		widgets.skyRotationY->setSingleStep(5.0);
		widgets.skyRotationY->setToolTip(QStringLiteral(
			"Spins the sky about the up axis. The image-based lighting turns with it, or the scene "
			"would be lit from where the sky used to be."));
		layout->addWidget(widgets.skyRotationY);

		layout->addSpacing(8);
		layout->addWidget(Heading(QStringLiteral("Exposure"), widgets.panel));

		widgets.overrideExposure =
			new QCheckBox(QStringLiteral("Override the derived exposure"), widgets.panel);
		widgets.overrideExposure->setToolTip(QStringLiteral(
			"The lighting bake normalizes every environment to middle grey, so on its own no "
			"environment can be dimmer or brighter than another.\nThis is where a dusk is told it "
			"is a dusk. A re-bake refreshes the derivation without touching it."));
		layout->addWidget(widgets.overrideExposure);

		widgets.exposure = new QDoubleSpinBox(widgets.panel);
		widgets.exposure->setRange(0.0, 100.0);
		widgets.exposure->setDecimals(3);
		widgets.exposure->setSingleStep(0.05);
		widgets.exposure->setEnabled(false);
		layout->addWidget(widgets.exposure);

		layout->addSpacing(8);
		layout->addWidget(Heading(QStringLiteral("Rim light"), widgets.panel));

		auto* rimNote = Caption(widgets.panel);
		rimNote->setText(QStringLiteral(
			"Sampled from this environment behind the surface, so it takes its colour. Only a mesh "
			"that opted in catches it -- the material editor's Rim lighting box."));
		layout->addWidget(rimNote);

		layout->addWidget(new QLabel(QStringLiteral("Tint"), widgets.panel));
		widgets.rimTint = new QPushButton(widgets.panel);
		SetTintSwatch(widgets.rimTint, glm::vec3(1.0f));
		layout->addWidget(widgets.rimTint);

		layout->addWidget(new QLabel(QStringLiteral("Intensity"), widgets.panel));
		widgets.rimIntensity = new QDoubleSpinBox(widgets.panel);
		widgets.rimIntensity->setRange(0.0, 100.0);
		widgets.rimIntensity->setDecimals(3);
		widgets.rimIntensity->setSingleStep(0.1);
		widgets.rimIntensity->setToolTip(
			QStringLiteral("Zero is a rim light that is off, whatever else is set."));
		layout->addWidget(widgets.rimIntensity);

		layout->addWidget(new QLabel(QStringLiteral("Falloff"), widgets.panel));
		widgets.rimPower = new QDoubleSpinBox(widgets.panel);
		widgets.rimPower->setRange(0.0, 64.0);
		widgets.rimPower->setDecimals(2);
		widgets.rimPower->setSingleStep(0.5);
		widgets.rimPower->setToolTip(
			QStringLiteral("Higher is a narrower band, held closer to the silhouette."));
		layout->addWidget(widgets.rimPower);

		layout->addStretch(1);

		// Named so a test can reach them: this panel's rules are worth pinning and the window's
		// widget pointers are private, which is what findChild is for.
		widgets.open->setObjectName(QStringLiteral("EnvOpen"));
		widgets.save->setObjectName(QStringLiteral("EnvSave"));
		widgets.skyMipLevel->setObjectName(QStringLiteral("EnvSkyMipLevel"));
		widgets.skyRotationY->setObjectName(QStringLiteral("EnvSkyRotationY"));
		widgets.overrideExposure->setObjectName(QStringLiteral("EnvOverrideExposure"));
		widgets.exposure->setObjectName(QStringLiteral("EnvExposure"));
		widgets.rimTint->setObjectName(QStringLiteral("EnvRimTint"));
		widgets.rimIntensity->setObjectName(QStringLiteral("EnvRimIntensity"));
		widgets.rimPower->setObjectName(QStringLiteral("EnvRimPower"));

		const std::array<QWidget*, 6> controls = { widgets.skyMipLevel,      widgets.skyRotationY,
			                                       widgets.overrideExposure, widgets.rimTint,
			                                       widgets.rimIntensity,     widgets.rimPower };
		for (QWidget* control : controls) control->setEnabled(false);

		return widgets;
	}
}
