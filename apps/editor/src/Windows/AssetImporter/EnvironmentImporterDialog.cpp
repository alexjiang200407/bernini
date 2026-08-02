#include "EnvironmentImporterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
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

	m_ImportLighting = new QCheckBox("Environment lighting", this);
	m_ImportLighting->setObjectName("importLighting");
	m_ImportLighting->setChecked(true);
	m_ImportLighting->setToolTip(
		"The image-based lighting: the specular and diffuse convolutions of the same radiance. "
		"Minutes of work, where the sky is moments -- uncheck it to re-author a backdrop without "
		"paying for the lighting again.");
	layout->addWidget(m_ImportLighting);

	m_ImportEnvironment = new QCheckBox("Environment (composes the two above)", this);
	m_ImportEnvironment->setObjectName("importEnvironment");
	m_ImportEnvironment->setChecked(true);
	m_ImportEnvironment->setToolTip(
		"A .benv naming whichever of the two were written. It holds no pixels, so it needs at "
		"least "
		"one of them.");
	layout->addWidget(m_ImportEnvironment);

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
EnvironmentImporterDialog::AssetName() const
{
	const QString typed = m_Name->text().trimmed();
	return IsPlainFileStem(typed) ? typed : m_DefaultName;
}
