#pragma once

#include <QString>

#include <assetlib/progress.h>
#include <bgl/IGraphics.h>

namespace editor::startup
{
	/**
	 * What the startup screen reads while `bgl` builds a pipeline -- the shader by name, because
	 * "Compiling shaders" for thirty seconds says nothing about whether it is stuck.
	 */
	[[nodiscard]] QString
	PipelineLabel(const bgl::PipelineProgress& progress);

	/**
	 * What it reads while a project's derived assets are rebuilt: the phase, and the container it
	 * is on. A key is shown by its file name -- the leading `Derived/Meshes/` is the same on every
	 * line and is what pushes the part that changes off the end of a narrow screen.
	 */
	[[nodiscard]] QString
	RebuildLabel(const assetlib::ProgressEvent& event);
}
