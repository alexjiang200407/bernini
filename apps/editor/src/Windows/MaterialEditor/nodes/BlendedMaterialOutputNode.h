#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"

// The alpha-blend sink: base color is RGBA like the cutout, but its alpha feeds the blend rather than
// a discard, so there is no cutoff row.
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
};
