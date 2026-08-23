#pragma once

#include <QString>
#include <QStringList>

#include <assetlib_structs/BMaterial.h>

#include "Async/BackgroundTask.h"

class MaterialGraphModel;
class QWidget;

namespace editor
{
	/**
	 * Whether `a` and `b` name the same material file.
	 *
	 * Not QFileInfo's own comparison: it falls back to canonicalFilePath(), which is *empty* for a
	 * file that does not exist -- so two different missing paths compare equal, and a material whose
	 * file has been deleted would match every mesh.
	 *
	 * weakly_canonical resolves the part of the path that does exist and normalises the rest, so it
	 * works either way. Case-insensitively, because this is a Windows tool (see the editor CLAUDE.md).
	 * An empty path matches nothing, itself included.
	 */
	[[nodiscard]] bool
	IsSameMaterialFile(const QString& a, const QString& b);

	/**
	 * A one-per-line listing of the baked textures `material` currently names -- base colour, normal and
	 * ORM -- or an empty string when it names none (never baked, or not a PBR material). An unrouted map
	 * shows as a dash. Shown read-only: the graph authors the routes these are composited from, and a
	 * bake -- this panel's Bake All, or the Content Explorer's -- is what rewrites them.
	 */
	[[nodiscard]] QString
	BakedTexturesSummary(const assetlib::BMaterial& material);

	/**
	 * The material `model` authors, ready to be written to `materialPath`.
	 *
	 * A material already on disk keeps whatever a previous bake produced: the triplet and its
	 * provenance. Rebuilding purely from the graph would throw the optimized textures away on every
	 * Save. If the routes have since changed, the stamps no longer match and the bake reports stale.
	 */
	[[nodiscard]] assetlib::BMaterial
	BuildMaterial(
		MaterialGraphModel&          model,
		const QString&               materialPath,
		const std::filesystem::path& dataRoot);

	/**
	 * Where a Save As should open, for a graph that has no file yet: `name` under the project's
	 * Materials directory, or under the data root when the project has none.
	 */
	[[nodiscard]] QString
	DefaultMaterialPath(const std::filesystem::path& dataRoot, const QString& name);

	/**
	 * The distinct material files among `paths`, first spelling first, with the empty ones dropped.
	 *
	 * Compared by IsSameMaterialFile rather than as strings, so one file reached two ways is one file:
	 * baking it twice decodes, resizes and re-encodes every map a second time to write what is already
	 * there.
	 */
	[[nodiscard]] QStringList
	UniqueMaterialFiles(const QStringList& paths);

	/** What one Save All wrote, and what it could not. */
	struct MaterialSaveResult
	{
		int saved = 0;

		// Graphs with no file yet. Skipped rather than prompted: a batch action that opens a file
		// dialog per submesh is not one.
		int unsaved = 0;

		QStringList failed;

		// Written, but the mesh could not be made to name them: the `.bmaterial` is on disk and the
		// mesh still points elsewhere.
		QStringList unattached;
	};

	/**
	 * What to tell the user after a Save All, or an empty string when a dialog would say nothing worth
	 * a click -- everything written and nothing skipped. The panel's own refresh reports a clean run.
	 */
	[[nodiscard]] QString
	MaterialSaveSummary(const MaterialSaveResult& result);

	/**
	 * Composites each of `materials` -- data-root-relative, as every asset reference is -- down to its
	 * baked triplet and rewrites it, reporting through `progress`.
	 *
	 * Reads each off disk, so what it bakes is the routes last saved. Throws out of the first failure
	 * or cancellation rather than carrying on: every map is named by the hash of its inputs, so a
	 * half-finished run leaves only correct, reusable files behind.
	 */
	void
	BakeMaterials(
		const std::filesystem::path& dataRoot,
		const QStringList&           materials,
		background::Progress&        progress);

	/**
	 * Derives a tangent for every submesh of the `.bmesh` at `meshPath` that has none, and rewrites
	 * the file. Asks first, and reports both a failure and a run that found nothing to do.
	 *
	 * @return true when the mesh was rewritten, and so must be reloaded for the new vertex layout to
	 *         reach the renderer. False when the user declined, nothing needed one, or it failed.
	 */
	[[nodiscard]] bool
	GenerateTangents(
		QWidget*                     parent,
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& meshPath);
}
