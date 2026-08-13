#include "Windows/AssetImporter/folder_row.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace editor
{
	QLineEdit*
	AddFolderRow(QVBoxLayout* layout, QWidget* parent, const FolderRowDesc& desc)
	{
		auto* row = new QHBoxLayout();
		row->addWidget(new QLabel(QString("%1/").arg(desc.category), parent));

		auto* field = new QLineEdit(desc.text, parent);
		field->setObjectName(desc.objectName);
		field->setPlaceholderText(desc.placeholder);
		field->setToolTip(desc.tip);
		row->addWidget(field, 1);

		auto* form = new QFormLayout();
		form->addRow(desc.label, row);
		layout->addLayout(form);

		return field;
	}
}
