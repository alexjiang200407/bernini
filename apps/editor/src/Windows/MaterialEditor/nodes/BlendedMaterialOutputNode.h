#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;

// The alpha-blend sink: base color is RGBA like the cutout, but its alpha feeds the blend rather than
// a discard. Its extra controls are the Occlude toggle and, for it, the cutoff its depth pre-pass
// discards below.
class BlendedMaterialOutputNode : public MaterialOutputNode
{
	Q_OBJECT

public:
	BlendedMaterialOutputNode();

	QString
	caption() const override
	{
		return QStringLiteral("Blended Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("BlendedMaterialOutput");
	}

	[[nodiscard]] assetlib::AlphaMode
	AlphaMode() const noexcept override
	{
		return assetlib::AlphaMode::kBlend;
	}

	[[nodiscard]] bool
	Occlude() const noexcept override
	{
		return m_Occlude;
	}

	// The pre-pass discard threshold; only consulted when Occlude is on.
	[[nodiscard]] float
	AlphaCutoff() const noexcept override
	{
		return m_AlphaCutoff;
	}

	QJsonObject
	save() const override;
	void
	load(const QJsonObject& json) override;

protected:
	void
	AddExtraRows(QWidget* parent, QFormLayout* form) override;

private:
	bool  m_Occlude     = false;
	float m_AlphaCutoff = 0.5f;

	QCheckBox*      m_OccludeBox = nullptr;
	QDoubleSpinBox* m_CutoffSpin = nullptr;
};
