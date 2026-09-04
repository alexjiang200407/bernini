#pragma once

#include "Render/environment.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/held_open_assets.h"
#include <qcontainerfwd.h>
#include <qtmetamacros.h>
#include <qwidget.h>

/**
 * The environment the level viewport is lit and backed by.
 *
 * Distinct from the material preview's, and deliberately so: the two viewports answer different
 * questions. A level is judged on the world it is in, so its backdrop is drawn sharp and its
 * lighting is whatever the level's own environment says.
 */
using LevelEditorEnv = editor::EnvironmentApplyDesc;

class LevelEditorWindow : public RenderTargetWindow, public editor::IHoldsAssets
{
	Q_OBJECT

public:
	LevelEditorWindow(
		QWidget*               parent = nullptr,
		RenderTargetWindowDesc desc   = {},
		LevelEditorEnv         env    = {});

	/** The `.benv` this view is lit by, which must not be deleted while it is still drawing it. */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

private:
	// The `.benv` bound and the slots it took, so nothing releases one the view still names.
	editor::EnvironmentBinding m_Environment;
};
