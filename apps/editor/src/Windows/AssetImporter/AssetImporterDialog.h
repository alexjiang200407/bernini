#pragma once

#include <QDialog>

#include <assetlib/bmesh_gltf.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace editor
{
	class ImportSection;
}

/**
 * Every file an import will write, and the one folder it cannot name file by file.
 *
 * Each path is relative to the project's data root and already inside its category --
 * `Meshes/animals/coyote.bmesh` -- because every reference in a project is written against that
 * layout: an import organises *within* a category and can never move an asset out of one.
 */
struct ImportOutputs
{
	QString mesh;        // the `.bmesh`
	QString skeleton;    // the `.bskel`, written only when the source carries a skin
	QString animations;  // the `.banim`, one file holding every clip

	/** The folder `materialStems` are written into. */
	QString materialDir;

	/**
	 * Where the extracted textures go, as a folder rather than files: `AssetStore::WriteTextures`
	 * names its output after the images they came from, so an import can neither name them nor --
	 * since two sources may name an image alike -- share the folder with another.
	 */
	QString textureDir;

	/**
	 * One `.bmaterial` stem per material in the source's table, index-aligned with it and empty where
	 * no file is written. See editor::MaterialStems.
	 */
	QStringList materialStems;
};

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	/**
	 * @param materials What probeGltfMaterials found in `sourceFile`, read only for the duration of
	 *        the constructor. The caller probes, so the dialog stays a dialog and a test can pose any
	 *        answer.
	 * @param dataRoot The project's data root, so a name landing on a file already there is refused
	 *        while it is typed rather than after OK. Empty checks nothing, which is what a test with
	 *        no project on disk wants.
	 */
	explicit AssetImporterDialog(
		const QString&                          sourceFile,
		std::span<const assetlib::GltfMaterial> materials = {},
		const QString&                          dataRoot  = {},
		QWidget*                                parent    = nullptr);

	/** Whether the geometry comes across. Off imports only the other pieces. */
	bool
	GetImportMesh() const;

	/** Whether any piece at all is coming across. */
	bool
	ImportsAnything() const;

	bool
	GetImportTextures() const;

	/** Whether to derive a `.bmaterial` from each of the glTF's PBR materials and bind it. */
	bool
	CanImportPbrMaterials() const;

	bool
	GetImportAnimations() const;

	/**
	 * Every file this import will write, as the fields currently read.
	 *
	 * Only meaningful once GetProblem is empty: a name that cannot be written is reported rather than
	 * quietly replaced, so what comes back here for one is not a path worth using.
	 */
	[[nodiscard]] ImportOutputs
	GetOutputs() const;

	/**
	 * Why the import cannot proceed as typed, or empty when it can. What the OK button follows, and
	 * what is shown beneath the sections.
	 *
	 * A folder falls back to the source's name when it cannot be honoured, but a *file name* does not:
	 * discarding a name the user deliberately typed is not the same as recovering a folder they left
	 * blank.
	 */
	[[nodiscard]] QString
	GetProblem() const;

private:
	/** One file the import would write as the fields currently read. */
	struct PlannedFile
	{
		QString subject;  // how a message names it -- "The mesh", "The material 'Fur'"
		QString name;     // the stem as typed, before any of it is judged
		QString path;     // data-root-relative, extension included
	};

	/** Every file that will actually be written, skipping the categories that are not coming across. */
	[[nodiscard]] std::vector<PlannedFile>
	PlanFiles() const;

	/** `category/<what was typed>`, or `category/<source name>` when the field cannot be honoured. */
	[[nodiscard]] QString
	Folder(const QLineEdit* field, const QString& category) const;

	/** The full path of one named file: its section's folder, the name, and a fixed extension. */
	[[nodiscard]] QString
	File(
		const editor::ImportSection* section,
		const QLineEdit*             name,
		const QString&               category,
		const QString&               extension) const;

	/** Re-runs the validation and moves the OK button and the message with it. */
	void
	Refresh();

	// Whether the source has anything to import; separate from the box, which is also off when
	// textures are.
	bool m_HasPbrMaterials = false;

	QCheckBox* m_ImportMesh         = nullptr;
	QCheckBox* m_ImportTextures     = nullptr;
	QCheckBox* m_ImportPbrMaterials = nullptr;
	QCheckBox* m_ImportAnimations   = nullptr;

	editor::ImportSection* m_MeshSection      = nullptr;
	editor::ImportSection* m_SkeletonSection  = nullptr;
	editor::ImportSection* m_TextureSection   = nullptr;
	editor::ImportSection* m_MaterialSection  = nullptr;
	editor::ImportSection* m_AnimationSection = nullptr;

	QLineEdit* m_MeshName      = nullptr;
	QLineEdit* m_SkeletonName  = nullptr;
	QLineEdit* m_AnimationName = nullptr;

	// One per material in the source's table, index-aligned with it and null where the material is not
	// PBR -- so a stem list built from this lines up with what the writer iterates.
	std::vector<QLineEdit*> m_MaterialNames;
	QStringList             m_MaterialLabels;

	QLabel*      m_Problem = nullptr;
	QPushButton* m_Ok      = nullptr;

	QString m_DefaultName;
	QString m_DataRoot;
};
