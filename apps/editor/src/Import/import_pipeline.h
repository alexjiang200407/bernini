#pragma once

#include "Windows/AssetImporter/AssetImporterDialog.h"

#include <QString>

class QWidget;

namespace editor
{
	/** What became of an import, so a multi-file drop knows whether to carry on with the next one. */
	enum class ImportOutcome
	{
		kImported,
		kCancelled,  // by the user, on the loading screen
		kBlocked,    // an asset of that name already exists; reported to the user
		kFailed,     // already reported to the user
	};

	/** What the import dialog asked for. */
	struct ImportOptions
	{
		// Every file to write, already inside its own category.
		ImportOutputs outputs;

		bool mesh         = true;  // off imports only the pieces below -- see ImportMesh
		bool textures     = false;
		bool pbrMaterials = false;  // ignored without textures -- a material routes at those
		bool animations   = false;  // the clips; the skeleton rides with the mesh
	};

	/**
	 * Converts a glTF/glb into the engine's on-disk form under `dataRoot`.
	 *
	 * Which category each piece lands in is decided by what it is, not by where a drop happened: the
	 * mesh under `Meshes/`, the rig under `Skeletons/`, the clips under `Animations/`.
	 * `options.outputs` says where inside each -- a project's references are written against that
	 * layout, so an import may organise within a category and never across one.
	 *
	 * Parsing and supercompressing the textures run on a worker thread behind a cancellable loading
	 * screen: they take long enough to freeze the editor. Nothing there touches bgl. The material
	 * graphs are built afterwards, back on the UI thread -- their nodes own QPixmaps, which belong to
	 * it -- so the `.bmesh` is written from the UI thread too, once its materials exist to be named.
	 *
	 * Refuses to overwrite anything, reports a failure to the user, and on either a failure or a cancel
	 * removes the half-written files it had produced -- see assetlib::rollBackImport.
	 *
	 * What counts as a collision differs by category. A materials folder may be shared with another
	 * import, since `options.outputs` names each file, so only a colliding *file* refuses this one. A
	 * texture folder may not: `writeTextures` names its output by index, so one already there is
	 * another import's and writing into it would overwrite that import's files.
	 *
	 * @param parent The widget the loading screen and every message box are parented to.
	 */
	[[nodiscard]] ImportOutcome
	ImportMesh(
		QWidget*             parent,
		const QString&       dataRoot,
		const QString&       sourceFile,
		const ImportOptions& options);

	/**
	 * Converts a Radiance `.hdr` into the environment family: a `.bsky`, the `.benvl` convolved from
	 * the same radiance, and the `.benv` naming the pair.
	 *
	 * Unlike ImportMesh there is no rollBackImport here -- `assetlib::importEnvironment` undoes its own
	 * half-written work, including on a cancel, so the editor has nothing to clean up after.
	 */
	[[nodiscard]] ImportOutcome
	ImportEnvironment(QWidget* parent, const QString& dataRoot, const QString& sourceFile);
}
