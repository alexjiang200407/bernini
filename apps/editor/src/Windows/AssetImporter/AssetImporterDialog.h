#pragma once

#include <QDialog>

#include <assetlib/bmesh_gltf.h>

class QCheckBox;
class QLineEdit;

/**
 * Where each piece of an import lands, relative to the project's data root.
 *
 * Each is already inside its category -- `Meshes/coyote`, `Animations/coyote` -- because every
 * reference in a project is written against that layout: a destination organises *within* a category
 * and can never move an asset out of one.
 */
struct ImportDestinations
{
	QString mesh;
	QString skeleton;
	QString animations;
	QString materials;
	QString textures;
};

class AssetImporterDialog : public QDialog
{
	Q_OBJECT

public:
	/**
	 * @param materials What probeGltfMaterials found in `sourceFile`, read only for the duration of
	 *        the constructor. The caller probes, so the dialog stays a dialog and a test can pose any
	 *        answer.
	 */
	explicit AssetImporterDialog(
		const QString&                          sourceFile,
		std::span<const assetlib::GltfMaterial> materials = {},
		QWidget*                                parent    = nullptr);

	/** Whether the geometry comes across. Off imports only the other pieces. */
	bool
	GetImportMesh() const;

	/** Whether any piece at all is coming across; what the OK button follows. */
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
	 * Where each category this import writes to puts its half of the import.
	 *
	 * Every field defaults to the source file's base name, and falls back to it when what was typed is
	 * empty or would escape its category. Each import wants a folder of its own, because
	 * `writeTextures` names its output `texN.ktx2` by index and two imports sharing one would
	 * overwrite each other.
	 */
	ImportDestinations
	GetDestinations() const;

private:
	/** `category/<what was typed>`, or `category/<source name>` when the field cannot be honoured. */
	[[nodiscard]] QString
	Destination(const QLineEdit* field, const QString& category) const;

	// Whether the source has anything to import; separate from the box, which is also off when
	// textures are.
	bool m_HasPbrMaterials = false;

	QCheckBox* m_ImportMesh         = nullptr;
	QCheckBox* m_ImportTextures     = nullptr;
	QCheckBox* m_ImportPbrMaterials = nullptr;
	QCheckBox* m_ImportAnimations   = nullptr;

	QLineEdit* m_MeshFolder      = nullptr;
	QLineEdit* m_SkeletonFolder  = nullptr;
	QLineEdit* m_TextureFolder   = nullptr;
	QLineEdit* m_MaterialFolder  = nullptr;
	QLineEdit* m_AnimationFolder = nullptr;

	QString m_DefaultFolder;
};
