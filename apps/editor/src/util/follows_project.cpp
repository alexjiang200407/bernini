#include "util/follows_project.h"

#include <QObject>

namespace editor
{
	void
	SetProjectDataRoot(QObject* root, const QString& dataRoot)
	{
		if (root == nullptr)
			return;

		const auto tell = [&dataRoot](QObject* object) {
			if (auto* follower = dynamic_cast<IFollowsProject*>(object))
				follower->SetDataRoot(dataRoot);
		};

		tell(root);
		for (QObject* child : root->findChildren<QObject*>()) tell(child);
	}
}
