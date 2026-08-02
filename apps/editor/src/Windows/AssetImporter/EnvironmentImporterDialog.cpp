#include "EnvironmentImporterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace
{
	/**
	 * Whether `name` is a plain file stem: the import joins it onto three category directories and a
	 * suffix, so anything that could redirect that join names a file outside the project's layout.
	 */
	bool
	IsPlainFileStem(const QString& name)
	{
		if (name.isEmpty())
			return false;

		static const QRegularExpression c_Unsafe(QStringLiteral("[^A-Za-z0-9_.-]"));
		if (name.contains(c_Unsafe))
			return false;

		// "." and ".." survive the character check and are not names.
		return name != "." && name != "..";
	}

	/**
	 * A relative folder that cannot climb out of the category it is joined onto. Anything that could
	 * re-root the join names a folder outside the layout, so it is refused and the category is used
	 * bare.
	 */
	bool
	IsContainedSubfolder(const QString& path)
	{
		if (path.isEmpty())
			return false;
		if (QDir::isAbsolutePath(path) || path.startsWith('/') || path.startsWith('\\'))
			return false;

		// operator/= replaces the left side when the right carries a differing root name, so a
		// drive-relative "D:" would re-root the join off the project. QDir does not call that absolute.
		if (path.contains(':'))
			return false;

		const QString cleaned = QDir::cleanPath(path);
		if (cleaned == ".." || cleaned.startsWith("../"))
			return false;

		return !cleaned.isEmpty() && cleaned != ".";
	}

	QString
	JoinCategory(const char* category, const QLineEdit& field)
	{
		const QString typed = field.text().trimmed();
		if (!IsContainedSubfolder(typed))
			return QString::fromLatin1(category);

		return QString("%1/%2").arg(QString::fromLatin1(category), QDir::cleanPath(typed));
	}
}

namespace
{
	/** A folder field behind its category, shown as an uneditable prefix so the layout is obvious. */
	QLineEdit*
	AddFolderRow(
		QVBoxLayout*   layout,
		QWidget*       parent,
		const char*    label,
		const char*    category,
		const char*    objectName,
		const QString& tip)
	{
		auto* row = new QHBoxLayout();
		row->addWidget(new QLabel(QString("%1/").arg(category), parent));

		auto* field = new QLineEdit(parent);
		field->setObjectName(objectName);
		field->setPlaceholderText("(optional subfolder)");
		field->setToolTip(tip);
		row->addWidget(field, 1);

		auto* form = new QFormLayout();
		form->addRow(label, row);
		layout->addLayout(form);

		return field;
	}
}

EnvironmentImporterDialog::EnvironmentImporterDialog(
	const QString& sourceFile,
	const QString& targetProject,
	QWidget*       parent) : QDialog(parent)
{
	setWindowTitle("Import Environment");
	setModal(true);

	m_DefaultName = QFileInfo(sourceFile).completeBaseName();

	auto* layout = new QVBoxLayout(this);

	auto* info = new QFormLayout();
	info->addRow("File:", new QLabel(sourceFile, this));
	info->addRow("Project:", new QLabel(targetProject, this));
	layout->addLayout(info);

	m_Name = new QLineEdit(m_DefaultName, this);
	m_Name->setObjectName("environmentName");
	m_Name->setPlaceholderText(m_DefaultName);
	m_Name->setToolTip(
		"Names every file this import writes: Sky/<name>.bsky, EnvLighting/<name>.benvl, "
		"Environments/<name>.benv and their sources.");

	auto* nameForm = new QFormLayout();
	nameForm->addRow("Name:", m_Name);
	layout->addLayout(nameForm);

	m_ImportSky = new QCheckBox("Sky", this);
	m_ImportSky->setObjectName("importSky");
	m_ImportSky->setChecked(true);
	m_ImportSky->setToolTip("The backdrop: one radiance cube map, projected from the source.");
	layout->addWidget(m_ImportSky);

	m_SkyDir = AddFolderRow(
		layout,
		this,
		"Sky folder:",
		"Sky",
		"skyDirectory",
		"Subfolder of Sky/ to write the .bsky into. The category itself is fixed: every reference "
		"in "
		"the project is written against it.");
	connect(m_ImportSky, &QCheckBox::toggled, m_SkyDir, &QWidget::setEnabled);

	m_ImportLighting = new QCheckBox("Environment lighting", this);
	m_ImportLighting->setObjectName("importLighting");
	m_ImportLighting->setChecked(true);
	m_ImportLighting->setToolTip(
		"The image-based lighting: the specular and diffuse convolutions of the same radiance. "
		"Minutes of work, where the sky is moments -- uncheck it to re-author a backdrop without "
		"paying for the lighting again.");
	layout->addWidget(m_ImportLighting);

	m_LightingDir = AddFolderRow(
		layout,
		this,
		"Lighting folder:",
		"EnvLighting",
		"lightingDirectory",
		"Subfolder of EnvLighting/ to write the .benvl into.");
	connect(m_ImportLighting, &QCheckBox::toggled, m_LightingDir, &QWidget::setEnabled);

	m_ImportEnvironment = new QCheckBox("Environment (composes the two above)", this);
	m_ImportEnvironment->setObjectName("importEnvironment");
	m_ImportEnvironment->setChecked(true);
	m_ImportEnvironment->setToolTip(
		"A .benv naming whichever of the two were written. It holds no pixels, so it needs at "
		"least "
		"one of them.");
	layout->addWidget(m_ImportEnvironment);

	m_SourceDir = AddFolderRow(
		layout,
		this,
		"Sources folder:",
		"textures_src",
		"sourceDirectory",
		"Subfolder of textures_src/ for the float intermediates each part is baked from. They are "
		"what a re-bake reads, so they are kept rather than being scratch.");

	// It composes the other two, so with neither there is nothing for it to name. Disabled rather
	// than left tickable, so the refusal is visible before OK rather than as an error afterwards.
	const auto refreshEnvironment = [this] {
		const bool available = m_ImportSky->isChecked() || m_ImportLighting->isChecked();
		m_ImportEnvironment->setEnabled(available);
	};
	connect(m_ImportSky, &QCheckBox::toggled, this, refreshEnvironment);
	connect(m_ImportLighting, &QCheckBox::toggled, this, refreshEnvironment);
	refreshEnvironment();

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	// Nothing selected is not an import. The box that can go dark is the environment's, so OK follows
	// the two that cannot.
	auto*      ok        = buttons->button(QDialogButtonBox::Ok);
	const auto refreshOk = [this, ok] {
		ok->setEnabled(m_ImportSky->isChecked() || m_ImportLighting->isChecked());
	};
	connect(m_ImportSky, &QCheckBox::toggled, this, refreshOk);
	connect(m_ImportLighting, &QCheckBox::toggled, this, refreshOk);
	refreshOk();
}

bool
EnvironmentImporterDialog::ImportSky() const
{
	return m_ImportSky->isChecked();
}

bool
EnvironmentImporterDialog::ImportLighting() const
{
	return m_ImportLighting->isChecked();
}

bool
EnvironmentImporterDialog::ImportEnvironment() const
{
	// Disabled means unavailable, whatever the box happens to be showing.
	return m_ImportEnvironment->isEnabled() && m_ImportEnvironment->isChecked();
}

QString
EnvironmentImporterDialog::SkyDirectory() const
{
	return JoinCategory("Sky", *m_SkyDir);
}

QString
EnvironmentImporterDialog::LightingDirectory() const
{
	return JoinCategory("EnvLighting", *m_LightingDir);
}

QString
EnvironmentImporterDialog::SourceDirectory() const
{
	return JoinCategory("textures_src", *m_SourceDir);
}

QString
EnvironmentImporterDialog::AssetName() const
{
	const QString typed = m_Name->text().trimmed();
	return IsPlainFileStem(typed) ? typed : m_DefaultName;
}
