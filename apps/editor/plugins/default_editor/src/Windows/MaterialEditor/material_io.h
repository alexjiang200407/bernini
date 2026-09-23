#pragma once
#include <assetlib/AssetStore.h>

#include <QString>
#include <QStringList>

#include <assetlib_structs/BMaterial.h>
#include <filesystem>
#include <qcontainerfwd.h>

#include <editor_sdk/BackgroundTask.h>

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
	 *
	 * The shading model is the sink's: a surface board writes a surface material and a PBR board a
	 * PBR one, so the board decides -- which is why OpenMaterialInto never puts a surface document
	 * behind a PBR board.
	 */
	[[nodiscard]] assetlib::BMaterial
	BuildMaterial(
		MaterialGraphModel&         model,
		const QString&              materialPath,
		const assetlib::AssetStore& store);

	/**
	 * Where a Save As should open, for a graph that has no file yet: `name` under the project's
	 * Materials directory, or under the data root when the project has none.
	 */
	[[nodiscard]] QString
	DefaultMaterialPath(const std::filesystem::path& dataRoot, const QString& name);

	/**
	 * Where a submesh's material is written when it has none yet and the panel saves by itself:
	 * `Materials/<mesh stem>/<submesh>.bmaterial`, or beside the project's Materials directory for
	 * a mesh with no file.
	 *
	 * Under the mesh's own directory because that is how a project keeps them (`Materials/Bear/`),
	 * and because a submesh name is only unique within its mesh -- two `Box[0]`s would otherwise
	 * write to one file. The name is `ToPlainFileStem`'d: a submesh may be called anything.
	 */
	[[nodiscard]] QString
	AutoSaveMaterialPath(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& meshPath,
		const QString&               submeshName);

	/**
	 * Everything a Material Editor showing `materials` over `previewMesh` holds open, absolute.
	 * The mesh counts: the panel binds materials into it and writes them back through the
	 * `.bmesh`. An empty path -- the default sphere -- holds nothing.
	 */
	[[nodiscard]] QStringList
	HeldOpenByMaterialEditor(
		const QStringList&           materials,
		const std::filesystem::path& previewMesh);

	/**
	 * The distinct material files among `paths`, first spelling first, with the empty ones dropped.
	 *
	 * Compared by IsSameMaterialFile rather than as strings, so one file reached two ways is one file:
	 * baking it twice decodes, resizes and re-encodes every map a second time to write what is already
	 * there.
	 */
	[[nodiscard]] QStringList
	UniqueMaterialFiles(const QStringList& paths);

	/** What one write of the edited graphs put on disk, and what it could not. */
	struct MaterialSaveResult
	{
		int saved = 0;

		QStringList failed;

		// Written, but the mesh could not be made to name them: the `.bmaterial` is on disk and the
		// mesh still points elsewhere.
		QStringList unattached;
	};

	/**
	 * What to tell the user after the panel wrote its edited graphs, or an empty string when a
	 * dialog would say nothing worth a click -- everything written. The panel's own refresh
	 * reports a clean run, and the writes happen on a timer nobody asked to be told about.
	 */
	[[nodiscard]] QString
	MaterialSaveSummary(const MaterialSaveResult& result);

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
		const assetlib::AssetStore&  store,
		const std::filesystem::path& meshPath);
}
