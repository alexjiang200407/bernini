#include "MaterialEditorWindow.h"

#include <QLabel>
#include <QVBoxLayout>

MaterialEditorWindow::MaterialEditorWindow(QWidget* parent) : QWidget(parent)
{
	// Nothing here yet: a centred placeholder until the material tools are built.
	auto* layout = new QVBoxLayout(this);
	auto* label  = new QLabel("Material Editor", this);
	label->setAlignment(Qt::AlignCenter);
	label->setStyleSheet("color: gray;");
	layout->addWidget(label);
}
