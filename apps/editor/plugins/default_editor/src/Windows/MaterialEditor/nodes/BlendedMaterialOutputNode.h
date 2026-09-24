#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include <assetlib_structs/BMaterial.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/localize.h>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qwidget.h>

class QDoubleSpinBox;

// The alpha-blend sink: base color is RGBA like the cutout, but its alpha feeds the blend rather than
// a discard. Transmission is what that alpha means -- see GetTransmission.
class BlendedMaterialOutputNode : public MaterialOutputNode
{
	Q_OBJECT

public:
	// `language` must outlive the node.
	explicit BlendedMaterialOutputNode(const editor::ILanguageResolver& language);

	QString
	caption() const override
	{
		return editor::Localize(
			m_Language,
			"bernini.material_nodes.blended_material_output_caption",
			"Blended Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("BlendedMaterialOutput");
	}

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept override
	{
		return assetlib::AlphaMode::kBlend;
	}

	[[nodiscard]] float
	GetTransmission() const noexcept override
	{
		return m_Transmission;
	}

	QJsonObject
	save() const override;
	void
	load(const QJsonObject& json) override;

protected:
	void
	AddExtraRows(QWidget* parent, QFormLayout* form) override;

private:
	float m_Transmission = 0.0f;

	QDoubleSpinBox* m_TransmissionSpin = nullptr;
};
