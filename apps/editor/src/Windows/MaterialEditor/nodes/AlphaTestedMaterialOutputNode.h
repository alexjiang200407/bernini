#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

class QDoubleSpinBox;

/**
 * The cutout sink: MaterialOutputNode with the base-color alpha channel added, plus the cutoff a
 * pixel's alpha must reach to survive.
 *
 * Ending a graph in this node -- rather than routing an alpha channel into the opaque one -- is what
 * makes the material a cutout. The alpha port exists on this node and nowhere else, so "routes alpha"
 * and "is alpha tested" cannot disagree.
 */
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
