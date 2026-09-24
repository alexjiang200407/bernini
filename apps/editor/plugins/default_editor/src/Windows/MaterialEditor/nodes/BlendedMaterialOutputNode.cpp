#include "Windows/MaterialEditor/nodes/BlendedMaterialOutputNode.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include <editor_plugin_api/ILanguageResolver.h>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QSignalBlocker>
#include <editor_plugin_api/localize.h>
#include <qtmetamacros.h>

BlendedMaterialOutputNode::BlendedMaterialOutputNode(const editor::ILanguageResolver& language) :
	MaterialOutputNode(
		language,
		ChannelData::c_MaxChannels)  // base color is RGBA: the alpha drives the blend
{}

void
BlendedMaterialOutputNode::AddExtraRows(QWidget* parent, QFormLayout* form)
{
	m_TransmissionSpin = new QDoubleSpinBox(parent);
	m_TransmissionSpin->setRange(0.0, 1.0);
	m_TransmissionSpin->setSingleStep(0.05);
	m_TransmissionSpin->setDecimals(3);
	m_TransmissionSpin->setValue(m_Transmission);
	m_TransmissionSpin->setToolTip(
		editor::Localize(
			m_Language,
			"bernini.material_nodes.transmission_tooltip",
			"What base-color alpha means: 0 is coverage (hair, foliage), 1 is "
			"transmission (glass), where the surface keeps its reflection however "
			"clear it is"));
	form->addRow(
		editor::Localize(m_Language, "bernini.material_nodes.transmission_label", "Transmission"),
		m_TransmissionSpin);

	connect(m_TransmissionSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		m_Transmission = static_cast<float>(value);
		Q_EMIT Changed();
	});
}

QJsonObject
BlendedMaterialOutputNode::save() const
{
	QJsonObject json     = MaterialOutputNode::save();
	json["transmission"] = m_Transmission;
	return json;
}

void
BlendedMaterialOutputNode::load(const QJsonObject& json)
{
	MaterialOutputNode::load(json);

	const QJsonValue transmission = json["transmission"];
	m_Transmission = transmission.isDouble() ? static_cast<float>(transmission.toDouble()) : 0.0f;

	// The spin box only exists once the node has been shown.
	if (m_TransmissionSpin != nullptr)
	{
		const QSignalBlocker blocker(m_TransmissionSpin);
		m_TransmissionSpin->setValue(m_Transmission);
	}
}
