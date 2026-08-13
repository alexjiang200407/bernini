#include "AssetImporterDialog.h"

#include "Project/Project.h"
#include "Windows/AssetImporter/folder_row.h"
#include "util/asset_paths.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AssetImporterDialog::AssetImporterDialog(
	const QString&                     sourceFile,
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

	// Every category gets its own field, each defaulting to the source's name -- so the layout an
	// import lands in is the same one it always was until a field is actually changed.
	const auto addFolder =
		[&](const char* label, const char* category, const char* objectName, const QString& tip) {
			return editor::AddFolderRow(
				layout,
				this,
				{ .label       = label,
		          .category    = category,
		          .objectName  = objectName,
		          .text        = m_DefaultFolder,
		          .placeholder = m_DefaultFolder,
		          .tip         = tip });
		};

	m_ImportMesh = new QCheckBox("Import mesh", this);
	m_ImportMesh->setObjectName("importMesh");
	m_ImportMesh->setChecked(true);
	m_ImportMesh->setToolTip(
		"Bring the geometry across. Off imports only the pieces below -- which is how a rig's "
		"clips "
		"arrive when the artist exported one file per animation, each carrying a copy of the "
		"mesh.");
	layout->addWidget(m_ImportMesh);

	m_MeshFolder = addFolder(
		"Mesh folder:",
		Project::c_MeshesDirectoryName,
		"meshFolder",
		"Folder under Meshes/ to write the .bmesh into. Nested folders are allowed "
		"(animals/coyote); the category itself is fixed, because every reference in the project is "
		"written against it.");

	// The rig rides with the geometry -- a skin is only meaningful against the mesh it deforms -- so
	// its folder follows the mesh box rather than one of its own.
	m_SkeletonFolder = addFolder(
		"Skeleton folder:",
		Project::c_SkeletonsDirectoryName,
		"skeletonFolder",
		"Folder under Skeletons/ to write the .bskel into. Only written when the source carries a "
		"skin.");

	m_ImportTextures = new QCheckBox("Import textures", this);
	m_ImportTextures->setObjectName("importTextures");
	m_ImportTextures->setChecked(true);
	m_ImportTextures->setToolTip("Extract the mesh's textures into the project.");
	layout->addWidget(m_ImportTextures);

	m_TextureFolder = addFolder(
		"Texture folder:",
		Project::c_TexturesSrcDirectoryName,
		"textureFolder",
		"Folder under textures_src/ for the extracted textures. Each import wants its own: they "
		"are named tex0.ktx2, tex1.ktx2 by index, so two imports sharing a folder would overwrite "
		"one another.");

	m_ImportPbrMaterials = new QCheckBox("Import PBR materials", this);
	m_ImportPbrMaterials->setObjectName("importPbrMaterials");
	m_ImportPbrMaterials->setToolTip(
		m_HasPbrMaterials ?
			"Derive a material from each of the glTF's PBR materials and bind it to the submeshes "
			"cut from it. Each is routed at this import's textures and can be reopened in the "
			"Material Editor." :
			"This file has no PBR material to derive one from.");
	layout->addWidget(m_ImportPbrMaterials);

	m_MaterialFolder = addFolder(
		"Material folder:",
		Project::c_MaterialsDirectoryName,
		"materialFolder",
		"Folder under Materials/ to write the derived .bmaterial files into.");

	m_ImportAnimations = new QCheckBox("Import animations", this);
	m_ImportAnimations->setObjectName("importAnimations");
	m_ImportAnimations->setChecked(false);
	m_ImportAnimations->setToolTip(
		"Bring the file's clips across. With the mesh off they attach to the rig already in the "
		"project, matched by signature.");
	layout->addWidget(m_ImportAnimations);

	m_AnimationFolder = addFolder(
		"Animation folder:",
		Project::c_AnimationsDirectoryName,
		"animationFolder",
		"Folder under Animations/ to write the .banim into.");

	// A derived material routes at the extracted textures, and is bound to the submeshes it was cut
	// from -- so it needs both those things to be coming across.
	const auto refreshMaterials = [this] {
		const bool available =
			m_HasPbrMaterials && m_ImportTextures->isChecked() && m_ImportMesh->isChecked();
		m_ImportPbrMaterials->setEnabled(available);
		m_ImportPbrMaterials->setChecked(available);
	};
	connect(m_ImportTextures, &QCheckBox::toggled, this, refreshMaterials);
	connect(m_ImportMesh, &QCheckBox::toggled, this, refreshMaterials);
	refreshMaterials();

	// A field is dead when nothing is going to be written through it.
	const auto refreshFolders = [this] {
		m_MeshFolder->setEnabled(m_ImportMesh->isChecked());
		m_SkeletonFolder->setEnabled(m_ImportMesh->isChecked());
		m_TextureFolder->setEnabled(m_ImportTextures->isChecked());
		m_MaterialFolder->setEnabled(CanImportPbrMaterials());
		m_AnimationFolder->setEnabled(m_ImportAnimations->isChecked());
	};
	connect(m_ImportMesh, &QCheckBox::toggled, this, refreshFolders);
	connect(m_ImportTextures, &QCheckBox::toggled, this, refreshFolders);
	connect(m_ImportPbrMaterials, &QCheckBox::toggled, this, refreshFolders);
	connect(m_ImportAnimations, &QCheckBox::toggled, this, refreshFolders);
	refreshFolders();

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// An import with every box clear parses the whole file behind a loading screen and writes
	// nothing, which is a success the user cannot tell from a failure.
	auto*      ok        = buttons->button(QDialogButtonBox::Ok);
	const auto refreshOk = [this, ok] { ok->setEnabled(ImportsAnything()); };
	connect(m_ImportMesh, &QCheckBox::toggled, this, refreshOk);
	connect(m_ImportTextures, &QCheckBox::toggled, this, refreshOk);
	connect(m_ImportAnimations, &QCheckBox::toggled, this, refreshOk);
	refreshOk();
}

bool
AssetImporterDialog::GetImportMesh() const
{
	return m_ImportMesh->isChecked();
}

bool
AssetImporterDialog::ImportsAnything() const
{
	return m_ImportMesh->isChecked() || m_ImportTextures->isChecked() ||
	       m_ImportAnimations->isChecked();
}

bool
AssetImporterDialog::GetImportTextures() const
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
AssetImporterDialog::GetImportAnimations() const
{
	return m_ImportAnimations->isChecked();
}

QString
AssetImporterDialog::Destination(const QLineEdit* field, const QString& category) const
{
	const QString typed = field->text().trimmed();
	return editor::JoinCategory(
		category,
		editor::IsContainedRelativePath(typed) ? typed : m_DefaultFolder);
}

ImportDestinations
AssetImporterDialog::GetDestinations() const
{
	return {
		.mesh       = Destination(m_MeshFolder, Project::c_MeshesDirectoryName),
		.skeleton   = Destination(m_SkeletonFolder, Project::c_SkeletonsDirectoryName),
		.animations = Destination(m_AnimationFolder, Project::c_AnimationsDirectoryName),
		.materials  = Destination(m_MaterialFolder, Project::c_MaterialsDirectoryName),
		.textures   = Destination(m_TextureFolder, Project::c_TexturesSrcDirectoryName),
	};
}
