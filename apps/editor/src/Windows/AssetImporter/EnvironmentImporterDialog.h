#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

/**
 * What to make of a dropped HDRI: a sky, the lighting convolved from it, and the environment naming
 * the pair.
 *
 * The three are separable because they cost different amounts. A sky is a projection and is done in
 * moments; the lighting is two convolutions and is not. Re-authoring a backdrop should not re-pay for
 * the lighting, and an existing sky can be given a `.benvl` later.
 *
 * A dialog only: it reads nothing off disk and starts nothing. The caller runs the import, so the
 * rules here can be tested without a file to import.
 */
class EnvironmentImporterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit EnvironmentImporterDialog(
		const QString& sourceFile,
		const QString& targetProject,
		QWidget*       parent = nullptr);

	bool
	ImportSky() const;

	bool
	ImportLighting() const;

	/**
	 * Whether to write the `.benv` composing whichever halves were selected.
	 *
	 * False whenever neither half is, however the box is showing: an environment composes the other
	 * two, so alone it would name nothing -- and `assetlib::importEnvironment` refuses that outright.
	 * The dialog cannot be allowed to ask for something the import will throw on.
	 */
	bool
	ImportEnvironment() const;

	/**
	 * The asset name every written file is named from: `Sky/<name>.bsky`, `textures_src/<name>_sky.ktx2`
	 * and so on.
	 *
	 * Falls back to the source's base name when what was typed could name a file outside the
	 * categories it belongs in -- a separator, or anything that is not a plain file stem.
	 */
	QString
	GetAssetName() const;

	/**
	 * Folder each part is written into, relative to the data root: the category it belongs in, plus
	 * whatever subfolder was typed.
	 *
	 * The category is a fixed prefix rather than something typed, because every reference in the
	 * project is written against that layout -- a `.benv` names `Sky/...`, and the content explorer
	 * protects the categories from deletion. A typed folder organises *inside* one; it cannot move a
	 * part out of it, and anything that could is ignored.
	 *
	 * There is deliberately no field for the `.benv` itself: `editor::ApplyEnvironment` finds a
	 * project's data root by taking the file's parent's parent, so an environment anywhere but
	 * directly inside `Environments/` would resolve its references against the wrong root.
	 */
	QString
	GetSkyDirectory() const;

	QString
	GetLightingDirectory() const;

	QString
	GetSourceDirectory() const;

private:
	QCheckBox* m_ImportSky         = nullptr;
	QCheckBox* m_ImportLighting    = nullptr;
	QCheckBox* m_ImportEnvironment = nullptr;
	QLineEdit* m_Name              = nullptr;
	QLineEdit* m_SkyDir            = nullptr;
	QLineEdit* m_LightingDir       = nullptr;
	QLineEdit* m_SourceDir         = nullptr;
	QString    m_DefaultName;
};
