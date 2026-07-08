#pragma once

#include <QDialog>

class QCheckBox;

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit AssetImporterDialog(
		const QString& sourceFile,
		const QString& targetDir,
		QWidget*       parent = nullptr);

	bool
	ImportTextures() const;

	bool
	ImportAnimations() const;

private:
	QCheckBox* m_ImportTextures   = nullptr;
	QCheckBox* m_ImportAnimations = nullptr;
};
