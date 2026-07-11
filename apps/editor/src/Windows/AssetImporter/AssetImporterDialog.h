#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	// The root every import's textures are written beneath, relative to the project's Data
	// directory. Shown as a fixed prefix on the destination field.
	static constexpr auto c_TextureRoot = "textures_src";

	explicit AssetImporterDialog(
		const QString& sourceFile,
		const QString& targetDir,
		QWidget*       parent = nullptr);

	bool
	ImportTextures() const;

	bool
	ImportAnimations() const;

	/**
	 * Folder beneath `c_TextureRoot` to write this import's textures into
	 *
	 * writeTextures names its output `texN.ktx2` by index, so two imports sharing a directory
	 * overwrite each other. Each import gets its own, defaulting to the source file's base name.
	 */
	QString
	TextureSubdirectory() const;

private:
	QCheckBox* m_ImportTextures   = nullptr;
	QCheckBox* m_ImportAnimations = nullptr;
	QLineEdit* m_TextureSubdir    = nullptr;
	QString    m_DefaultSubdir;
};
