#pragma once

#include <QString>

namespace editor
{
	/** The editor's window title: the parts that are there, joined with an em dash. */
	[[nodiscard]] QString
	WindowTitle(const QString& instanceName, const QString& projectName);
}
