#pragma once

#include <QDialog>

#include <assetlib/bmesh_gltf.h>

class QCheckBox;
class QLineEdit;

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	/**
	 * @param materials What probeGltfMaterials found in `sourceFile`. The dialog only reads it -- the
	 *        caller probes, so the dialog stays a dialog and a test can pose any answer.
	 */
	explicit AssetImporterDialog(
		const QString&                     sourceFile,
		const assetlib::GltfMaterialProbe& materials = {},
		QWidget*                           parent    = nullptr);

	/** Whether the geometry comes across. Off imports only the other pieces. */
	bool
	ImportMesh() const;

	/** Whether any piece at all is coming across; what the OK button and the folder field follow. */
	bool
	ImportsAnything() const;

	bool
	ImportTextures() const;

	/** Whether to derive a `.bmaterial` from each of the glTF's PBR materials and bind it. */
	bool
	CanImportPbrMaterials() const;

	bool
	ImportAnimations() const;

	/**
	 * Folder this import organises itself into, *inside* each category it writes to -- the mesh under
	 * `Meshes/`, the rig under `Skeletons/`, and so on. May name nested folders (`animals/coyote`);
	 * may never escape a category, which JoinCategory enforces.
	 *
	 * Defaults to the source file's base name. Each import wants its own, because `writeTextures`
	 * names its output `texN.ktx2` by index and two imports sharing a folder would overwrite one
	 * another.
	 */
	QString
	DestinationFolder() const;

private:
	// Whether the source has anything to import; separate from the box, which is also off when
	// textures are.
	bool m_HasPbrMaterials = false;

	QCheckBox* m_ImportMesh         = nullptr;
	QCheckBox* m_ImportTextures     = nullptr;
	QCheckBox* m_ImportPbrMaterials = nullptr;
	QCheckBox* m_ImportAnimations   = nullptr;
	QLineEdit* m_Folder             = nullptr;
	QString    m_DefaultFolder;
};
