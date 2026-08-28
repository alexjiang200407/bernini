#include "util/held_open_assets.h"

#include <QObject>

namespace editor
{
	QStringList
	GetAssetsHeldOpen(const QObject* root)
	{
		if (root == nullptr)
			return {};

		auto held = QStringList();

		const auto declare = [&held](const QObject* object) {
			if (const auto* holder = dynamic_cast<const IHoldsAssets*>(object))
				held += holder->GetHeldOpenPaths();
		};

		declare(root);
		for (const QObject* child : root->findChildren<QObject*>()) declare(child);

		return held;
	}
}
