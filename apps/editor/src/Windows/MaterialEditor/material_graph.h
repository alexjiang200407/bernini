#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>

#include <QtNodes/NodeDelegateModelRegistry>

#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMaterialImport.h>
#include <bgl/IScene.h>
#include <filesystem>
#include <memory>
#include <optional>

class MaterialGraphModel;
class TexturePreviewCache;
class Renderer;

/**
 * Rewrites `path` relative to `dir`, or resolves it against `dir` when `toRelative` is false.
 *
 * A `.bmaterial`'s texture references are relative to the project's Data root -- not to the material
 * file -- so a material names `Derived/SourceTextures/albedo.ktx2` and
 * `Derived/BakedTextures/orm_ab12.ktx2` whatever directory
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
 * Puts a saved board in a fixed order: nodes by id, connections by the endpoints they join.
 *
 * QtNodes keeps its connections in an unordered set, so the order they come out of `save()` in
 * follows insertion -- and a board built by an import, one loaded from a file, and one a person
 * has edited all insert differently. Without this a material rewrites itself on every save with
 * no change in it, which costs a diff on every open and costs `migrate` the byte-compare it
 * decides staleness with.
 */
void
SortGraph(QJsonObject& graph);

/**
 * The node types a material graph can hold.
 *
 * `renderer` and `previews` may be null: a TextureNode then shows no image, which is what lets a graph
 * be built and compiled with no graphics device.
 */
[[nodiscard]] std::shared_ptr<QtNodes::NodeDelegateModelRegistry>
MakeMaterialNodeRegistry(Renderer* renderer, TexturePreviewCache* previews);

/**
 * Compiles `model` into the material it authors: the factors and alpha mode of its sink, the nine
 * routes wired into it, and the graph itself as `editorGraph` so reopening restores the board. Texture
 * paths are stored relative to `dataRoot`, like every asset reference.
 *
 * The routes are read back out of the graph rather than tracked beside it, so a material's routes and
 * the board that produced them cannot disagree.
 *
 * The result carries no baked triplet: a graph authors routes, and nothing here has run a bake, so it
 * draws from those routes. A caller rewriting a material that already exists on disk must carry the
 * previous bake's triplet and stamps across itself.
 */
[[nodiscard]] assetlib::BMaterial
CompileMaterial(
	MaterialGraphModel&          model,
	const QString&               name,
	const std::filesystem::path& dataRoot);

/**
 * The scene point a graph should be centred on: the middle of its output node, not its corner -- a
 * node centred by its corner hangs off the left of the panel. Empty for a graph with no sink.
 */
[[nodiscard]] std::optional<QPointF>
OutputCentre(MaterialGraphModel& model);

/** The maps a glTF material names, as absolute paths. Empty where it names none. */
struct ImportedMaterialMaps
{
	QString baseColor;
	QString normal;
	QString orm;

	// glTF's own occlusion map. Equal to `orm` under the shared-ORM convention, and empty for a
	// material that names none.
	QString occlusion;
};

/**
 * Lays out the board a glTF material describes in `model`, which must be empty: a Texture node per
 * distinct map it names, wired into the ports of a sink of the alpha mode the material declares.
 * A map feeds its group's one wide port, except where an occlusion map of its own forces the ORM
 * group apart -- then AO, roughness and metallic are wired a channel at a time.
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
