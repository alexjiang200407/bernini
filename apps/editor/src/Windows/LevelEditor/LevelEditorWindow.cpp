#include "LevelEditorWindow.h"

LevelEditorWindow::LevelEditorWindow(
	QWidget*               parent,
	RenderTargetWindowDesc desc,
	LevelEditorEnv         env) : RenderTargetWindow(parent, std::move(desc))
{
	if (env.environmentMap.empty() || GetRenderer() == nullptr)
		return;

	GetRenderer()->Invoke([&] {
		// No mip override: a level viewport shows the world sharp, where the material preview
		// defocuses its backdrop on purpose.
		m_Environment = editor::ApplyEnvironment(
			PreviewScene(),
			PreviewView(),
			env.environmentMap,
			env.dataRoot,
			env.exposureOverride,
			std::nullopt,
			"LevelEditor");
	});
}
