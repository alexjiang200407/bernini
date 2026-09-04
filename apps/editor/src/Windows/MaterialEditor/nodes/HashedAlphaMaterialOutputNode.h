#pragma once

#include "Windows/MaterialEditor/nodes/MaterialOutputNode.h"
#include <assetlib_structs/BMaterial.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qwidget.h>

/**
 * The sink for a surface whose alpha is stochastic coverage rather than a cutoff -- hair, foliage,
 * anything that self-occludes.
 *
 * It carries no cutoff of its own: a threshold is the thing this replaces, and every fragment is
 * tested against a per-pixel hashed one instead. It needs temporal AA to be running to resolve, which
 * is the one thing an author has to know about it.
 */
class HashedAlphaMaterialOutputNode : public MaterialOutputNode
{
	Q_OBJECT

public:
	HashedAlphaMaterialOutputNode();

	QString
	caption() const override
	{
		return QStringLiteral("Hashed Alpha Material Output");
	}

	QString
	name() const override
	{
		return QStringLiteral("HashedAlphaMaterialOutput");
	}

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept override
	{
		return assetlib::AlphaMode::kHashed;
	}

	// The base-color port is RGBA, and the alpha it carries is a coverage probability.
	[[nodiscard]] bool
	IsAlphaTested() const noexcept override
	{
		return true;
	}

protected:
	void
	AddExtraRows(QWidget* parent, QFormLayout* form) override;
};
