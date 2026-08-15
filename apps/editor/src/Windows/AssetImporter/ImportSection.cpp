#include "Windows/AssetImporter/ImportSection.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace editor
{
	namespace
	{
		QString
		FileCount(int files)
		{
			return files == 1 ? QStringLiteral("1 file") : QStringLiteral("%1 files").arg(files);
		}
	}

	ImportSection::ImportSection(QVBoxLayout* layout, QWidget* parent, const FolderRowDesc& desc) :
		QWidget(parent)
	{
		auto* own = new QVBoxLayout(this);
		own->setContentsMargins(0, 0, 0, 0);

		m_Toggle = new QToolButton(this);
		m_Toggle->setAutoRaise(true);
		m_Toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		m_Toggle->setToolTip("Show the files this import will write here.");
		m_Toggle->hide();
		connect(m_Toggle, &QToolButton::clicked, this, [this] { SetExpanded(m_Body->isHidden()); });

		m_Folder = AddFolderRow(own, this, desc, m_Toggle);

		m_Body  = new QWidget(this);
		m_Files = new QFormLayout(m_Body);

		// Indented under the folder it belongs to, so a file row is visibly a detail of the row above
		// rather than another category.
		m_Files->setContentsMargins(24, 0, 0, 0);
		m_Body->hide();
		own->addWidget(m_Body);

		layout->addWidget(this);
	}

	QLineEdit*
	ImportSection::AddFile(const FileRowDesc& desc)
	{
		auto* row = new QHBoxLayout();

		auto* field = new QLineEdit(desc.stem, m_Body);
		field->setObjectName(desc.objectName);
		field->setPlaceholderText(desc.stem);
		field->setToolTip(desc.tip);
		row->addWidget(field, 1);
		row->addWidget(new QLabel(desc.extension, m_Body));

		m_Files->addRow(desc.label, row);

		m_Toggle->setText(FileCount(m_Files->rowCount()));
		m_Toggle->show();
		SetExpanded(!m_Body->isHidden());

		return field;
	}

	QLineEdit*
	ImportSection::GetFolder() const
	{
		return m_Folder;
	}

	void
	ImportSection::SetExpanded(bool expanded)
	{
		m_Toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
		m_Body->setVisible(expanded);
	}
}
