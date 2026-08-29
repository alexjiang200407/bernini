#pragma once

#include <QString>

#include <assetlib/progress.h>

namespace editor::startup
{
	/**
	 * What the startup screen reads while a project's derived assets are rebuilt: the phase, and
	 * the container it is on. A key is shown by its file name -- the leading `Derived/Meshes/` is the same on every
	 * line and is what pushes the part that changes off the end of a narrow screen.
	 */
	[[nodiscard]] QString
	RebuildLabel(const assetlib::ProgressEvent& event);
}
