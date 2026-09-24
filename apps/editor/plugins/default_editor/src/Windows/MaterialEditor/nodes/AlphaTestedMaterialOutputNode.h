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

class AlphaTestedMaterialOutputNode : public MaterialOutputNode
{
	Q_OBJECT

public:
	// `language` must outlive the node.
	explicit AlphaTestedMaterialOutputNode(const editor::ILanguageResolver& language);

	QString
	caption() const override
	{
		return editor::Localize(
			m_Language,
			"bernini.material_nodes.alpha_tested_material_output_caption",
			"Alpha Tested Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("AlphaTestedMaterialOutput");
	}

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept override
	{
		return assetlib::AlphaMode::kMask;
	}

	[[nodiscard]] bool
	IsAlphaTested() const noexcept override
	{
		return true;
	}

	[[nodiscard]] float
	GetAlphaCutoff() const noexcept override
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
