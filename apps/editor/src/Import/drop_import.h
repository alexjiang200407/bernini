#pragma once

#include "Import/import_pipeline.h"

#include <QString>

class QMimeData;
class QWidget;

namespace editor
{
	/** What one mesh source's import produced. */
	struct MeshImport
	{
		ImportOutcome outcome = ImportOutcome::kFailed;

		/**
		 * The `.bmesh` it wrote, data-root-relative, or empty when it wrote none -- an import asked
		 * for the clips alone, and every outcome but kImported.
		 */
		QString mesh;
	};

	/**
	 * Asks for `sourceFile`'s import options and runs the import behind its loading screen.
	 *
	 * The whole of what importing one mesh source means, so that a drop of several and a caller
	 * holding one file agree on it: the self-contained check that refuses before a dialog promises
	 * an import that cannot happen, the material probe that decides what the dialog may offer, the
	 * dialog, and the cook.
	 *
	 * @param parent Parents the dialog, the loading screen and every message box.
	 */
	[[nodiscard]] MeshImport
	RunMeshImport(QWidget* parent, const QString& dataRoot, const QString& sourceFile);

	/** Whether `localFile` names a mesh source the importer accepts. */
	[[nodiscard]] bool
	IsImportableMesh(const QString& localFile);

	/**
	 * Whether `localFile` names an environment source the importer accepts.
	 *
	 * Radiance HDR only, though ImportEnvironment also reads a float cube `.ktx2`: a `.ktx2` dragged
	 * in is far more likely to be a texture, and guessing wrong would import one as a sky.
	 */
	[[nodiscard]] bool
	IsImportableEnvironment(const QString& localFile);

	/** Whether `mime` carries at least one local file an import could take. */
	[[nodiscard]] bool
	AcceptsImportDrop(const QMimeData& mime);

	/**
	 * Imports every file in `mime` that an import can take, asking for each one's options in turn.
	 *
	 * An environment is not written into any dropped-on folder: its three parts each belong in their
	 * own category, so a drop names the project rather than a destination.
	 *
	 * Cancelling one import of a multi-file drop abandons the rest. Carrying on would answer the
	 * user's "stop" by immediately putting the next options dialog in front of them.
	 */
	void
	RunImportDrop(QWidget* parent, const QString& dataRoot, const QMimeData& mime);
}
