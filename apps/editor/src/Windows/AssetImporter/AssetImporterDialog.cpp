#include "AssetImporterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

AssetImporterDialog::AssetImporterDialog(
	const QString& sourceFile,
	const QString& targetDir,
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
	m_ImportTextures->setToolTip(
		"Extract the mesh's textures into the project's textures_src folder.");
	layout->addWidget(m_ImportTextures);

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
