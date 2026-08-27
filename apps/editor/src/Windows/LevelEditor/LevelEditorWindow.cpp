#include "LevelEditorWindow.h"

LevelEditorWindow::LevelEditorWindow(
	QWidget*               parent,
	RenderTargetWindowDesc desc,
	LevelEditorEnv         env) : RenderTargetWindow(parent, std::move(desc))
{
	m_Environment.configured = std::move(env);

	if (m_Environment.configured.environmentMap.empty() || GetRenderer() == nullptr)
		return;

	GetRenderer()->Invoke([&] {
		editor::BindEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			m_Environment,
			m_Environment.configured.environmentMap,
			m_Environment.configured.dataRoot,
			"LevelEditor");
	});
}

QStringList
LevelEditorWindow::GetHeldOpenPaths() const
{
	return editor::GetHeldOpenEnvironment(m_Environment);
}
