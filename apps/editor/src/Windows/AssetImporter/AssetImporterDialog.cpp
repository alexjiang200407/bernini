#include "AssetImporterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace
{
	// A relative folder that cannot climb out of the texture root. A typed path only ever names a
	// folder inside the project, so anything that could re-root the join is rejected.
	bool
	IsContainedRelativePath(const QString& path)
	{
		if (path.isEmpty() || QDir::isAbsolutePath(path))
			return false;

		// std::filesystem::path::operator/= replaces the left side when the right carries a root name
		// that differs from it, so a drive-relative "D:" would re-root the join off the project.
		// QDir::isAbsolutePath does not consider that absolute. A leading separator is a root too.
		if (path.contains(':') || path.startsWith('/') || path.startsWith('\\'))
			return false;

		const QString cleaned = QDir::cleanPath(path);
		if (cleaned == ".." || cleaned.startsWith("../"))
			return false;

		return !cleaned.isEmpty() && cleaned != ".";
	}
}

AssetImporterDialog::AssetImporterDialog(
	const QString& sourceFile,
	const QString& targetDir,
	QWidget*       parent) : QDialog(parent)
{
	setWindowTitle("Import Asset");
	setModal(true);

	m_DefaultSubdir = QFileInfo(sourceFile).completeBaseName();

	auto* layout = new QVBoxLayout(this);

	auto* info = new QFormLayout();
	info->addRow("File:", new QLabel(sourceFile, this));
	info->addRow("Destination:", new QLabel(targetDir, this));
	layout->addLayout(info);

	m_ImportTextures = new QCheckBox("Import textures", this);
	m_ImportTextures->setChecked(true);
	m_ImportTextures->setToolTip("Extract the mesh's textures into the project.");
	layout->addWidget(m_ImportTextures);

	// The root is fixed and shown as an uneditable prefix, so it is obvious the folder is created
	// inside the project's texture tree rather than anywhere the text could name.
	auto* textureRow = new QHBoxLayout();
	textureRow->addWidget(new QLabel(QString("%1/").arg(c_TextureRoot), this));

	m_TextureSubdir = new QLineEdit(m_DefaultSubdir, this);
	m_TextureSubdir->setPlaceholderText(m_DefaultSubdir);
	m_TextureSubdir->setToolTip(
		"Folder for this import's textures. Each import needs its own: the extracted files are "
		"named tex0.ktx2, tex1.ktx2 and so on by index, so two imports sharing a folder would "
		"overwrite one another.");
	textureRow->addWidget(m_TextureSubdir, 1);

	auto* textureForm = new QFormLayout();
	textureForm->addRow("Textures:", textureRow);
	layout->addLayout(textureForm);

	// The destination is meaningless when nothing is being extracted.
	connect(m_ImportTextures, &QCheckBox::toggled, m_TextureSubdir, &QWidget::setEnabled);

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
AssetImporterDialog::TextureSubdirectory() const
{
	const QString typed = m_TextureSubdir->text().trimmed();
	if (!IsContainedRelativePath(typed))
		return m_DefaultSubdir;

	return QDir::cleanPath(typed);
}
