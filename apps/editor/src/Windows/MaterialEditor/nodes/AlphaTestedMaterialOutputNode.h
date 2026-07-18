#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

class QDoubleSpinBox;

class AlphaTestedMaterialOutputNode : public MaterialOutputNode
{
	Q_OBJECT

public:
	AlphaTestedMaterialOutputNode();

	QString
	caption() const override
	{
		return QStringLiteral("Alpha Tested Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("AlphaTestedMaterialOutput");
	}

	[[nodiscard]] assetlib::AlphaMode
	AlphaMode() const noexcept override
	{
		return assetlib::AlphaMode::kMask;
	}

	[[nodiscard]] bool
	IsAlphaTested() const noexcept override
	{
		return true;
	}

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
	float m_AlphaCutoff = 0.5f;

	QDoubleSpinBox* m_CutoffSpin = nullptr;
};
