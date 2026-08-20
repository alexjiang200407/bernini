#include "AssetImporterDialog.h"

#include "Windows/AssetImporter/ImportSection.h"
#include "Windows/AssetImporter/material_stems.h"
#include "util/asset_paths.h"
#include <assetlib/Project.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
	constexpr auto c_MeshExtension     = ".bmesh";
	constexpr auto c_SkeletonExtension = ".bskel";
	constexpr auto c_AnimExtension     = ".banim";
	constexpr auto c_MaterialExtension = ".bmaterial";

	/** How a message names the material at `index`, which the glTF may have left unnamed. */
	QString
	MaterialLabel(const assetlib::GltfMaterial& material, size_t index)
	{
		const QString name = QString::fromStdString(material.name).trimmed();
		return name.isEmpty() ? QString("Material %1").arg(index) : name;
	}
}

AssetImporterDialog::AssetImporterDialog(
	const QString&                          sourceFile,
	std::span<const assetlib::GltfMaterial> materials,
	const QString&                          dataRoot,
	QWidget*                                parent) : QDialog(parent)
{
	setWindowTitle("Import Asset");
	setModal(true);

	m_DefaultName     = QFileInfo(sourceFile).completeBaseName();
	m_DataRoot        = dataRoot;
	m_HasPbrMaterials = std::ranges::any_of(materials, &assetlib::GltfMaterial::isPbr);

	auto* layout = new QVBoxLayout(this);

	auto* info = new QFormLayout();
	info->addRow("File:", new QLabel(sourceFile, this));
	layout->addLayout(info);

	// The sections scroll, because one unfolded over a source carrying thirty materials would
	// otherwise put the OK button below the bottom of the screen.
	auto* content   = new QWidget(this);
	auto* contents  = new QVBoxLayout(content);
	auto* scrollBox = new QScrollArea(this);
	contents->setContentsMargins(0, 0, 0, 0);
	scrollBox->setWidget(content);
	scrollBox->setWidgetResizable(true);
	scrollBox->setFrameShape(QFrame::NoFrame);
	scrollBox->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
	if (const QScreen* screen = QGuiApplication::primaryScreen())
		scrollBox->setMaximumHeight(screen->availableGeometry().height() * 2 / 3);
	layout->addWidget(scrollBox);

	// Every category gets its own section, each folder defaulting to the source's name -- so the
	// layout an import lands in is the one it always was until a field is actually changed.
	const auto addSection =
		[&](const char* label, const char* category, const char* objectName, const QString& tip) {
			auto* section = new editor::ImportSection(
				contents,
				content,
				{ .label       = label,
		          .category    = category,
		          .objectName  = objectName,
		          .text        = m_DefaultName,
		          .placeholder = m_DefaultName,
		          .tip         = tip });

			connect(section, &editor::ImportSection::Expanded, this, &QDialog::adjustSize);
			return section;
		};

	m_ImportMesh = new QCheckBox("Import mesh", content);
	m_ImportMesh->setObjectName("importMesh");
	m_ImportMesh->setChecked(true);
	m_ImportMesh->setToolTip(
		"Bring the geometry across. Off imports only the pieces below -- which is how a rig's "
		"clips "
		"arrive when the artist exported one file per animation, each carrying a copy of the "
		"mesh.");
	contents->addWidget(m_ImportMesh);

	m_MeshSection = addSection(
		"Mesh folder:",
		assetlib::c_MeshesDirectoryName,
		"meshFolder",
		"Folder under Meshes/ to write the .bmesh into. Nested folders are allowed "
		"(animals/coyote); the category itself is fixed, because every reference in the project is "
		"written against it.");
	m_MeshName = m_MeshSection->AddFile(
		{ .label      = "Mesh file:",
	      .stem       = m_DefaultName,
	      .extension  = c_MeshExtension,
	      .objectName = "meshName",
	      .tip = "What the geometry is written as. Naming it rather than taking the source's "
	             "name is what lets two imports share one folder." });

	// The rig rides with the geometry -- a skin is only meaningful against the mesh it deforms -- so
	// its folder follows the mesh box rather than one of its own.
	m_SkeletonSection = addSection(
		"Skeleton folder:",
		assetlib::c_SkeletonsDirectoryName,
		"skeletonFolder",
		"Folder under Skeletons/ to write the .bskel into. Only written when the source carries a "
		"skin.");
	m_SkeletonName = m_SkeletonSection->AddFile(
		{ .label      = "Skeleton file:",
	      .stem       = m_DefaultName,
	      .extension  = c_SkeletonExtension,
	      .objectName = "skeletonName",
	      .tip = "What the rig is written as, when the source carries one. A rig shared by several "
	             "files wants the same name in each of them." });

	m_ImportTextures = new QCheckBox("Import textures", content);
	m_ImportTextures->setObjectName("importTextures");
	m_ImportTextures->setChecked(true);
	m_ImportTextures->setToolTip("Extract the mesh's textures into the project.");
	contents->addWidget(m_ImportTextures);

	// No file rows: writeTextures names its output by index, so there is nothing here to name.
	m_TextureSection = addSection(
		"Texture folder:",
		assetlib::c_TexturesSrcDirectoryName,
		"textureFolder",
		"Folder under textures_src/ for the extracted textures. Each import wants its own: they "
		"are named tex0.ktx2, tex1.ktx2 by index, so two imports sharing a folder would overwrite "
		"one another.");

	m_ImportPbrMaterials = new QCheckBox("Import PBR materials", content);
	m_ImportPbrMaterials->setObjectName("importPbrMaterials");
	m_ImportPbrMaterials->setToolTip(
		m_HasPbrMaterials ?
			"Derive a material from each of the glTF's PBR materials and bind it to the submeshes "
			"cut from it. Each is routed at this import's textures and can be reopened in the "
			"Material Editor." :
			"This file has no PBR material to derive one from.");
	contents->addWidget(m_ImportPbrMaterials);

	m_MaterialSection = addSection(
		"Material folder:",
		assetlib::c_MaterialsDirectoryName,
		"materialFolder",
		"Folder under Materials/ to write the derived .bmaterial files into. Materials may share "
		"one with another import, since each names its own files.");

	// Only the PBR ones: a material the writer skips would otherwise be offered a name for a file
	// that never appears.
	const QStringList stems = editor::MaterialStems(materials);
	m_MaterialNames.resize(materials.size(), nullptr);

	for (size_t i = 0; i < materials.size(); ++i)
	{
		m_MaterialLabels << MaterialLabel(materials[i], i);
		if (!materials[i].isPbr)
			continue;

		m_MaterialNames[i] = m_MaterialSection->AddFile(
			{ .label      = m_MaterialLabels.back() + ':',
		      .stem       = stems[static_cast<qsizetype>(i)],
		      .extension  = c_MaterialExtension,
		      .objectName = QString("materialName%1").arg(i),
		      .tip        = "What this glTF material is written as." });
	}

	m_ImportAnimations = new QCheckBox("Import animations", content);
	m_ImportAnimations->setObjectName("importAnimations");
	m_ImportAnimations->setChecked(false);
	m_ImportAnimations->setToolTip(
		"Bring the file's clips across. With the mesh off they attach to the rig already in the "
		"project, matched by signature.");
	contents->addWidget(m_ImportAnimations);

	m_AnimationSection = addSection(
		"Animation folder:",
		assetlib::c_AnimationsDirectoryName,
		"animationFolder",
		"Folder under Animations/ to write the .banim into.");
	m_AnimationName = m_AnimationSection->AddFile(
		{ .label      = "Animation file:",
	      .stem       = m_DefaultName,
	      .extension  = c_AnimExtension,
	      .objectName = "animationName",
	      .tip = "What the clips are written as. Every clip in the source goes into this one file; "
	             "the Animation Editor picks one out of it." });

	contents->addStretch(1);

	m_Problem = new QLabel(this);
	m_Problem->setWordWrap(true);
	m_Problem->setStyleSheet("color: palette(link-visited);");
	m_Problem->hide();
	layout->addWidget(m_Problem);

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

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	m_Ok = buttons->button(QDialogButtonBox::Ok);

	for (QCheckBox* box :
	     { m_ImportMesh, m_ImportTextures, m_ImportPbrMaterials, m_ImportAnimations })
		connect(box, &QCheckBox::toggled, this, &AssetImporterDialog::Refresh);

	// Every field, folder and name alike: a folder decides which directory a name is checked against,
	// so moving one can make a name that was fine collide.
	for (const QLineEdit* field : findChildren<QLineEdit*>())
		connect(field, &QLineEdit::textChanged, this, &AssetImporterDialog::Refresh);

	Refresh();
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
AssetImporterDialog::Folder(const QLineEdit* field, const QString& category) const
{
	const QString typed = field->text().trimmed();
	return editor::JoinCategory(
		category,
		editor::IsContainedRelativePath(typed) ? typed : m_DefaultName);
}

QString
AssetImporterDialog::File(
	const editor::ImportSection* section,
	const QLineEdit*             name,
	const QString&               category,
	const QString&               extension) const
{
	return Folder(section->GetFolder(), category) + '/' + name->text().trimmed() + extension;
}

std::vector<AssetImporterDialog::PlannedFile>
AssetImporterDialog::PlanFiles() const
{
	auto planned = std::vector<PlannedFile>();

	const auto plan = [&](const QString&               subject,
	                      const editor::ImportSection* section,
	                      const QLineEdit*             name,
	                      const QString&               category,
	                      const QString&               extension) {
		planned.push_back(
			{ .subject = subject,
		      .name    = name->text().trimmed(),
		      .path    = File(section, name, category, extension) });
	};

	if (m_ImportMesh->isChecked())
	{
		plan(
			"The mesh",
			m_MeshSection,
			m_MeshName,
			assetlib::c_MeshesDirectoryName,
			c_MeshExtension);

		// Planned whether or not the source turns out to carry a skin: that is not known until it is
		// parsed, and by then a file already there cannot be told from one this import wrote.
		plan(
			"The skeleton",
			m_SkeletonSection,
			m_SkeletonName,
			assetlib::c_SkeletonsDirectoryName,
			c_SkeletonExtension);
	}

	if (m_ImportAnimations->isChecked())
	{
		plan(
			"The animations",
			m_AnimationSection,
			m_AnimationName,
			assetlib::c_AnimationsDirectoryName,
			c_AnimExtension);
	}

	if (CanImportPbrMaterials())
	{
		for (qsizetype i = 0; i < static_cast<qsizetype>(m_MaterialNames.size()); ++i)
		{
			if (m_MaterialNames[static_cast<size_t>(i)] == nullptr)
				continue;

			plan(
				QString("The material '%1'").arg(m_MaterialLabels[i]),
				m_MaterialSection,
				m_MaterialNames[static_cast<size_t>(i)],
				assetlib::c_MaterialsDirectoryName,
				c_MaterialExtension);
		}
	}

	return planned;
}

QString
AssetImporterDialog::GetProblem() const
{
	const std::vector<PlannedFile> planned = PlanFiles();

	auto claimed = QHash<QString, QString>();

	for (const PlannedFile& file : planned)
	{
		if (file.name.isEmpty())
			return QString("%1 needs a name.").arg(file.subject);

		if (!editor::IsPlainFileStem(file.name))
			return QString("'%1' cannot be a file name.").arg(file.name);

		// Case-insensitively, because two names differing only in case are one file on Windows.
		const QString key   = file.path.toLower();
		const auto    first = claimed.constFind(key);
		if (first != claimed.constEnd())
		{
			return QString("%1 and %2 would both be written as '%3'.")
			    .arg(*first, file.subject.at(0).toLower() + file.subject.mid(1), file.path);
		}

		claimed.insert(key, file.subject);
	}

	if (m_DataRoot.isEmpty())
		return {};

	for (const PlannedFile& file : planned)
	{
		if (QFileInfo::exists(m_DataRoot + '/' + file.path))
			return QString("'%1' is already in the project. Import never overwrites.")
			    .arg(file.path);
	}

	return {};
}

ImportOutputs
AssetImporterDialog::GetOutputs() const
{
	auto outputs = ImportOutputs();

	outputs.mesh =
		File(m_MeshSection, m_MeshName, assetlib::c_MeshesDirectoryName, c_MeshExtension);
	outputs.skeleton = File(
		m_SkeletonSection,
		m_SkeletonName,
		assetlib::c_SkeletonsDirectoryName,
		c_SkeletonExtension);
	outputs.animations = File(
		m_AnimationSection,
		m_AnimationName,
		assetlib::c_AnimationsDirectoryName,
		c_AnimExtension);

	outputs.materialDir =
		Folder(m_MaterialSection->GetFolder(), assetlib::c_MaterialsDirectoryName);
	outputs.textureDir =
		Folder(m_TextureSection->GetFolder(), assetlib::c_TexturesSrcDirectoryName);

	for (QLineEdit* name : m_MaterialNames)
		outputs.materialStems << (name == nullptr ? QString() : name->text().trimmed());

	return outputs;
}

void
AssetImporterDialog::Refresh()
{
	// A field is dead when nothing is going to be written through it. Disabling the section takes its
	// file names with it, which is what keeps a dead name out of the validation.
	m_MeshSection->setEnabled(m_ImportMesh->isChecked());
	m_SkeletonSection->setEnabled(m_ImportMesh->isChecked());
	m_TextureSection->setEnabled(m_ImportTextures->isChecked());
	m_MaterialSection->setEnabled(CanImportPbrMaterials());
	m_AnimationSection->setEnabled(m_ImportAnimations->isChecked());

	const QString problem = GetProblem();
	m_Problem->setText(problem);
	m_Problem->setVisible(!problem.isEmpty());

	// An import with every box clear parses the whole file behind a loading screen and writes
	// nothing, which is a success the user cannot tell from a failure.
	m_Ok->setEnabled(ImportsAnything() && problem.isEmpty());
}
