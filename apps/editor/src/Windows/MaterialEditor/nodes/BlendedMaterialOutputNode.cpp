#include "Windows/MaterialEditor/nodes/BlendedMaterialOutputNode.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QSignalBlocker>

BlendedMaterialOutputNode::BlendedMaterialOutputNode() :
	MaterialOutputNode(
		ChannelData::c_MaxChannels)  // base color is RGBA: the alpha drives the blend
{}

void
BlendedMaterialOutputNode::AddExtraRows(QWidget* parent, QFormLayout* form)
{
	m_OccludeBox = new QCheckBox(parent);
	m_OccludeBox->setChecked(m_Occlude);
	m_OccludeBox->setToolTip(QStringLiteral(
		"Draw a depth pre-pass so the surface hides its own back layers -- the look "
		"you want for hair and foliage, not glass."));
	form->addRow(QStringLiteral("Occlude"), m_OccludeBox);

	m_CutoffSpin = new QDoubleSpinBox(parent);
	m_CutoffSpin->setRange(0.0, 1.0);
	m_CutoffSpin->setSingleStep(0.05);
	m_CutoffSpin->setDecimals(3);
	m_CutoffSpin->setValue(m_AlphaCutoff);
	m_CutoffSpin->setEnabled(m_Occlude);
	m_CutoffSpin->setToolTip(QStringLiteral(
		"Base-color alpha below this does not write depth in the pre-pass, so it does not occlude. "
		"Lower it for hair whose strands are faint; raise it to keep only the densest core."));
	form->addRow(QStringLiteral("Occlude Cutoff"), m_CutoffSpin);

	connect(m_OccludeBox, &QCheckBox::toggled, this, [this](bool checked) {
		m_Occlude = checked;
		if (m_CutoffSpin != nullptr)
		{
			m_CutoffSpin->setEnabled(checked);
		}
		Q_EMIT Changed();
	});

	connect(m_CutoffSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_AlphaCutoff = static_cast<float>(value);
		Q_EMIT Changed();
	});
}

QJsonObject
BlendedMaterialOutputNode::save() const
{
	QJsonObject json    = MaterialOutputNode::save();
	json["occlude"]     = m_Occlude;
	json["alphaCutoff"] = m_AlphaCutoff;
	return json;
}

void
BlendedMaterialOutputNode::load(const QJsonObject& json)
{
	MaterialOutputNode::load(json);

	m_Occlude = json["occlude"].toBool(false);

	const QJsonValue cutoff = json["alphaCutoff"];
	m_AlphaCutoff           = cutoff.isDouble() ? static_cast<float>(cutoff.toDouble()) : 0.5f;

	// The widgets only exist once the node has been shown.
	if (m_OccludeBox != nullptr)
	{
		const QSignalBlocker blocker(m_OccludeBox);
		m_OccludeBox->setChecked(m_Occlude);
	}
	if (m_CutoffSpin != nullptr)
	{
		const QSignalBlocker blocker(m_CutoffSpin);
		m_CutoffSpin->setValue(m_AlphaCutoff);
		m_CutoffSpin->setEnabled(m_Occlude);
	}
}
