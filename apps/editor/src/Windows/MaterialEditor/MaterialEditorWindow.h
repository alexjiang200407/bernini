#pragma once

#include <QWidget>

// Placeholder for the upcoming material-authoring tools. Empty for now; it exists so the
// editor's tabbed dock layout already has its slot.
class MaterialEditorWindow : public QWidget
{
	Q_OBJECT

public:
	explicit MaterialEditorWindow(QWidget* parent = nullptr);
};
