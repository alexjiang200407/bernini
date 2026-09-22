#include "util/held_open_assets.h"

#include <QObject>
#include <QString>
#include <editor_plugin_api/EditorPanel.h>
#include <qcontainerfwd.h>
#include <string>

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
			if (const auto* panel = dynamic_cast<const EditorPanel*>(object))
				for (const std::string& key : panel->GetHeldAssets())
					held.push_back(QString::fromUtf8(key));
		};

		declare(root);
		for (const QObject* child : root->findChildren<QObject*>()) declare(child);

		return held;
	}
}
