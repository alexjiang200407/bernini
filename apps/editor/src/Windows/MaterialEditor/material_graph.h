#pragma once

#include <QJsonObject>
#include <QString>

#include <QtNodes/NodeDelegateModelRegistry>

#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMaterialImport.h>

class MaterialGraphModel;
class TexturePreviewCache;

namespace bgl
{
	class IScene;
}

/**
 * Rewrites `path` relative to `dir`, or resolves it against `dir` when `toRelative` is false.
 *
 * A `.bmaterial`'s texture references are relative to the project's Data root -- not to the material
 * file -- so a material names `textures_src/tex1.ktx2` and `Textures/orm_ab12.ktx2` whatever directory
 * it lives in. Texture nodes hold absolute paths while the graph is live, so the saved graph is
 * rewritten on the way out and back in. An empty `dir` (no project open) leaves the path alone, as do
 * paths that cannot be expressed relative to it -- a different drive, say. Both stay absolute: still
 * correct, merely not relocatable.
 */
[[nodiscard]] QString
Rebase(const QString& path, const std::filesystem::path& dir, bool toRelative);

/**
 * Rewrites every Texture node's stored path in a saved graph. QtNodes nests each delegate's own save()
 * under "internal-data", which is where TextureNode wrote its "texture" key.
 */
void
RebaseGraphTextures(QJsonObject& graph, const std::filesystem::path& dir, bool toRelative);

/**
 * The node types a material graph can hold.
 *
 * `scene` and `previews` may be null: a TextureNode then shows no image, which is what lets a graph be
 * built and compiled with no graphics device.
 */
[[nodiscard]] std::shared_ptr<QtNodes::NodeDelegateModelRegistry>
MakeMaterialNodeRegistry(bgl::IScene* scene, TexturePreviewCache* previews);

/**
 * Compiles `model` into the material it authors: the factors and alpha mode of its sink, the nine
 * routes wired into it, and the graph itself as `editorGraph` so reopening restores the board. Texture
 * paths are stored relative to `dataRoot`, like every asset reference.
 *
 * The routes are read back out of the graph rather than tracked beside it, so a material's routes and
 * the board that produced them cannot disagree.
 *
 * The result is always kLoose and carries no baked triplet: a graph authors routes, and nothing here
 * has run a bake. A caller rewriting a material that already exists on disk must carry the previous
 * bake's triplet, mode and stamps across itself.
 */
[[nodiscard]] assetlib::BMaterial
CompileMaterial(
	MaterialGraphModel&          model,
	const QString&               name,
	const std::filesystem::path& dataRoot);

/** The maps a glTF material names, as absolute paths. Empty where it names none. */
struct ImportedMaterialMaps
{
	QString baseColor;
	QString normal;
	QString orm;
};

/**
 * Lays out the board a glTF material describes in `model`, which must be empty: a Texture node per map
 * it names, each wired whole into its group's port on a sink of the alpha mode the material declares.
 *
 * The graph is the material -- CompileMaterial reads the routes back out of it, so this is the only
 * place that decides what a glTF material routes where, and there is no second table to disagree with.
 * Paths are absolute here, as in a live graph; CompileMaterial rebases them on the way to disk.
 */
void
BuildImportedMaterialGraph(
	MaterialGraphModel&                   model,
	const assetlib::imp::BMaterialImport& material,
	const ImportedMaterialMaps&           maps);
