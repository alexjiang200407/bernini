#include "LevelEditorWindow.h"

LevelEditorWindow::LevelEditorWindow(QWidget* parent, RenderTargetWindowDesc desc) :
	RenderTargetWindow(parent, std::move(desc))
{}
