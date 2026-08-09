#include "AssetImporterDialog.h"

#include "util/asset_paths.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

AssetImporterDialog::AssetImporterDialog(
	const QString&                     sourceFile,
	const QString&                     targetDir,
	const assetlib::GltfMaterialProbe& materials,
	QWidget*                           parent) : QDialog(parent)
{
	setWindowTitle("Import Asset");
	setModal(true);

	m_DefaultFolder   = QFileInfo(sourceFile).completeBaseName();
	m_HasPbrMaterials = materials.pbrMaterialCount > 0;

	auto* layout = new QVBoxLayout(this);

	auto* info = new QFormLayout();
	info->addRow("File:", new QLabel(sourceFile, this));
	layout->addLayout(info);

	m_ImportMesh = new QCheckBox("Import mesh", this);
	m_ImportMesh->setObjectName("importMesh");
	m_ImportMesh->setChecked(true);
	m_ImportMesh->setToolTip(
		"Bring the geometry across. Off imports only the pieces below -- which is how a rig's "
		"clips "
		"arrive when the artist exported one file per animation, each carrying a copy of the "
		"mesh.");
	layout->addWidget(m_ImportMesh);

	m_ImportTextures = new QCheckBox("Import textures", this);
	m_ImportTextures->setObjectName("importTextures");
	m_ImportTextures->setChecked(true);
	m_ImportTextures->setToolTip("Extract the mesh's textures into the project.");
	layout->addWidget(m_ImportTextures);

	// The category is the fixed part and is shown as an uneditable prefix: what is typed organises
	// inside `Meshes/`, `Skeletons/`, `Textures/` and the rest, and can never name a way out of one.
	auto* folderRow = new QHBoxLayout();
	folderRow->addWidget(new QLabel("<category>/", this));

	m_Folder = new QLineEdit(m_DefaultFolder, this);
	m_Folder->setPlaceholderText(m_DefaultFolder);
	m_Folder->setToolTip(
		"Folder this import organises itself into, inside each category it writes to -- the mesh "
		"under Meshes/, the rig under Skeletons/, the clips under Animations/. Nested folders are "
		"allowed (animals/coyote). Each import wants its own: extracted textures are named "
		"tex0.ktx2, tex1.ktx2 by index, so two imports sharing a folder would overwrite one "
		"another.");
	folderRow->addWidget(m_Folder, 1);

	auto* folderForm = new QFormLayout();
	folderForm->addRow("Folder:", folderRow);
	layout->addLayout(folderForm);

	m_ImportPbrMaterials = new QCheckBox("Import PBR materials", this);
	m_ImportPbrMaterials->setObjectName("importPbrMaterials");
	m_ImportPbrMaterials->setToolTip(
		m_HasPbrMaterials ?
			"Derive a material from each of the glTF's PBR materials and bind it to the submeshes "
			"cut from it. Each is routed at this import's textures and can be reopened in the "
			"Material Editor." :
			"This file has no PBR material to derive one from.");
	layout->addWidget(m_ImportPbrMaterials);

	// A derived material routes at the extracted textures, so it cannot come across without them.
	const auto refreshMaterials = [this](bool importingTextures) {
		const bool available = m_HasPbrMaterials && importingTextures;
		m_ImportPbrMaterials->setEnabled(available);
		m_ImportPbrMaterials->setChecked(available);
	};
	connect(m_ImportTextures, &QCheckBox::toggled, this, refreshMaterials);
	refreshMaterials(m_ImportTextures->isChecked());

	m_ImportAnimations = new QCheckBox("Import animations", this);
	m_ImportAnimations->setObjectName("importAnimations");
	m_ImportAnimations->setChecked(false);
	m_ImportAnimations->setToolTip(
		"Bring the file's clips across. With the mesh off they attach to the rig already in the "
		"project, matched by signature.");
	layout->addWidget(m_ImportAnimations);

	// The folder names where every piece lands, so it is dead only when no piece is coming.
	const auto refreshFolder = [this] {
		m_Folder->setEnabled(
			m_ImportMesh->isChecked() || m_ImportTextures->isChecked() ||
			m_ImportAnimations->isChecked());
	};
	connect(m_ImportMesh, &QCheckBox::toggled, this, refreshFolder);
	connect(m_ImportTextures, &QCheckBox::toggled, this, refreshFolder);
	connect(m_ImportAnimations, &QCheckBox::toggled, this, refreshFolder);
	refreshFolder();

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

bool
AssetImporterDialog::ImportGeometry() const
{
	return m_ImportMesh->isChecked();
}

bool
AssetImporterDialog::ImportTextures() const
{
	return m_ImportTextures->isChecked();
}

bool
AssetImporterDialog::CanImportPbrMaterials() const
{
	// Disabled means unavailable, whatever the box happens to be showing.
	return m_ImportPbrMaterials->isEnabled() && m_ImportPbrMaterials->isChecked();
}

bool
AssetImporterDialog::ImportAnimations() const
{
	return m_ImportAnimations->isChecked();
}

QString
AssetImporterDialog::DestinationFolder() const
{
	const QString typed = m_Folder->text().trimmed();
	if (!editor::IsContainedRelativePath(typed))
		return m_DefaultFolder;

	return QDir::cleanPath(typed);
}
