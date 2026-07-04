#include "AssetImporterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

AssetImporterDialog::AssetImporterDialog(
	const QString& sourceFile,
	const QString& targetDir,
	const QString& defaultTexturesDir,
	QWidget*       parent) : QDialog(parent)
{
	setWindowTitle("Import Asset");
	setModal(true);

	auto* layout = new QVBoxLayout(this);

	auto* info = new QFormLayout();
	info->addRow("File:", new QLabel(sourceFile, this));
	info->addRow("Destination:", new QLabel(targetDir, this));
	layout->addLayout(info);

	m_ImportTextures = new QCheckBox("Import textures", this);
	m_ImportTextures->setChecked(true);
	m_ImportTextures->setToolTip("Extract the mesh's textures into the folder below.");
	layout->addWidget(m_ImportTextures);

	auto* texturesForm = new QFormLayout();
	m_TexturesDir      = new QLineEdit(defaultTexturesDir, this);
	texturesForm->addRow("Textures folder:", m_TexturesDir);
	layout->addLayout(texturesForm);

	// The textures folder is only editable while textures are being imported.
	m_TexturesDir->setEnabled(m_ImportTextures->isChecked());
	connect(m_ImportTextures, &QCheckBox::toggled, m_TexturesDir, &QWidget::setEnabled);

	m_ImportAnimations = new QCheckBox("Import animations", this);
	m_ImportAnimations->setChecked(false);
	layout->addWidget(m_ImportAnimations);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

bool
AssetImporterDialog::ImportTextures() const
{
	return m_ImportTextures->isChecked();
}

bool
AssetImporterDialog::ImportAnimations() const
{
	return m_ImportAnimations->isChecked();
}

QString
AssetImporterDialog::TexturesDirectory() const
{
	return m_TexturesDir->text();
}
