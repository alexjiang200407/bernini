#pragma once

#include "Render/environment.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "ui_LevelEditorWindow.h"

/**
 * The environment the level viewport is lit and backed by.
 *
 * Distinct from the material preview's, and deliberately so: the two viewports answer different
 * questions. A level is judged on the world it is in, so its backdrop is drawn sharp and its
 * lighting is whatever the level's own environment says.
 */
struct LevelEditorEnv
{
	std::string environmentMap;

	// What the paths inside that `.benv` resolve against; see MaterialPreviewEnv::dataRoot.
	std::filesystem::path dataRoot;

	// Absent means the exposure the `.benv` carries, which is the value derived from its maps.
	std::optional<float> exposureOverride;
};

class LevelEditorWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	LevelEditorWindow(
		QWidget*               parent = nullptr,
		RenderTargetWindowDesc desc   = {},
		LevelEditorEnv         env    = {});

private:
	// What the environment bound, so nothing releases a slot the view still names.
	editor::AppliedEnvironment m_Environment;
};
