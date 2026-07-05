#pragma once

#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "ui_LevelEditorWindow.h"

class LevelEditorWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	explicit LevelEditorWindow(QWidget* parent = nullptr, RenderTargetWindowDesc desc = {});
};
