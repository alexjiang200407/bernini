#include "Windows/MaterialEditor/nodes/AlphaTestedMaterialOutputNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QSignalBlocker>

AlphaTestedMaterialOutputNode::AlphaTestedMaterialOutputNode() :
	MaterialOutputNode(ChannelData::c_MaxChannels)  // base color is RGBA here, not RGB
{}

void
AlphaTestedMaterialOutputNode::AddExtraRows(QWidget* parent, QFormLayout* form)
{
	m_CutoffSpin = new QDoubleSpinBox(parent);
	m_CutoffSpin->setRange(0.0, 1.0);
	m_CutoffSpin->setSingleStep(0.05);
	m_CutoffSpin->setDecimals(3);
	m_CutoffSpin->setValue(m_AlphaCutoff);
	m_CutoffSpin->setToolTip(QStringLiteral("Pixels whose base-color alpha is below this are cut"));
	form->addRow(QStringLiteral("Alpha Cutoff"), m_CutoffSpin);

	connect(m_CutoffSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_AlphaCutoff = static_cast<float>(value);
		Q_EMIT Changed();
	});
}

QJsonObject
AlphaTestedMaterialOutputNode::save() const
{
	QJsonObject json    = MaterialOutputNode::save();
	json["alphaCutoff"] = m_AlphaCutoff;
	return json;
}

void
AlphaTestedMaterialOutputNode::load(const QJsonObject& json)
{
	MaterialOutputNode::load(json);

	const QJsonValue cutoff = json["alphaCutoff"];
	m_AlphaCutoff           = cutoff.isDouble() ? static_cast<float>(cutoff.toDouble()) : 0.5f;

	// The spin box only exists once the node has been shown.
	if (m_CutoffSpin != nullptr)
	{
		const QSignalBlocker blocker(m_CutoffSpin);
		m_CutoffSpin->setValue(m_AlphaCutoff);
	}
}
