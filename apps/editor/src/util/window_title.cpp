#include "util/window_title.h"

#include <QStringList>
#include <qobject.h>
#include <qstringliteral.h>

namespace editor
{
	QString
	WindowTitle(const QString& instanceName, const QString& projectName)
	{
		auto parts = QStringList();

		// Trimmed here rather than at the config: whitespace is what a key filled in by hand or by a
		// script leaves behind, and a blank one must read as "no instance", not as a leading dash.
		if (const QString instance = instanceName.trimmed(); !instance.isEmpty())
			parts << instance;

		parts << QStringLiteral("Bernini Editor");

		if (const QString project = projectName.trimmed(); !project.isEmpty())
			parts << project;

		return parts.join(QStringLiteral(" — "));
	}
}
