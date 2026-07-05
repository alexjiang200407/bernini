#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit AssetImporterDialog(
		const QString& sourceFile,
		const QString& targetDir,
		const QString& defaultTexturesDir,
		QWidget*       parent = nullptr);

	bool
	ImportTextures() const;

	bool
	ImportAnimations() const;

	QString
	TexturesDirectory() const;

private:
	QCheckBox* m_ImportTextures   = nullptr;
	QCheckBox* m_ImportAnimations = nullptr;
	QLineEdit* m_TexturesDir      = nullptr;
};
