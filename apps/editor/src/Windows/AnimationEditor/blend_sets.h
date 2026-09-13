#pragma once

#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
	/**
	 * Every `.bblend` in `graph` authored against the clip set at `animationsKey`, sorted.
	 *
	 * A query over its `kBlendClips` edges, for the reason `ResolveAnimationBindings` is one over
	 * `kClipSkeleton`: matching, path normalization and the error policy have one home, and the
	 * panel already knows the clip set it is playing. An empty `animationsKey` resolves to nothing.
	 *
	 * Takes the scanned graph for the same reason that one does: the two are asked together on
	 * every load, and a scan reads and parses every asset in the project.
	 */
	[[nodiscard]] std::vector<std::string>
	ResolveBlendSets(const assetlib::AssetRefGraph& graph, std::string_view animationsKey);

	/**
	 * Every `.bmesh` in `graph` that can show the set at `blendSetKey`, sorted: the meshes skinned to
	 * the rig its clip set was resampled against.
	 *
	 * Two edges forward and one back -- `kBlendClips`, `kClipSkeleton`, then `kMeshSkeleton` -- because
	 * a set names its clip set and no mesh, and nothing else records one. Empty for a key that is not
	 * a set, a clip set that is not on disk or records no rig, and a rig nothing is skinned to.
	 */
	[[nodiscard]] std::vector<std::string>
	ResolveBlendSetMeshes(const assetlib::AssetRefGraph& graph, std::string_view blendSetKey);

	/**
	 * Every clip set in `graph` a new blend set can be started on, sorted: each `.banim` under the
	 * animations directory with nothing yet at `assetlib::blendSetKeyFor`'s key.
	 *
	 * A set stored anywhere else does not take the clip set, because that is not where
	 * `CreateEmptyBlendSet` writes -- what this answers is whether that write would be refused.
	 */
	[[nodiscard]] std::vector<std::string>
	ClipSetsWithoutBlendSet(const assetlib::AssetRefGraph& graph);

	/**
	 * Writes the empty set -- no spaces -- for the clip set at `animationsKey`, at the key
	 * `assetlib::blendSetKeyFor` names, through the project's store. Returns that key.
	 *
	 * The key is `assetlib::blendSetKeyFor`'s -- the convention belongs to the library that owns the
	 * layout, not to the panel that happens to be the first caller.
	 *
	 * Empty rather than seeded with a space over every clip: which clips belong in one run and what
	 * parameter each plays at is the authoring, and a guess would be a set someone has to correct
	 * rather than write. The codec allows it deliberately -- a set with no spaces "is the empty
	 * document a *create* writes, the way an avatar with no legs is".
	 *
	 * Free of the window for the reason `CreateEmptyAvatar` is: the one thing the action cannot
	 * afford to get wrong is the path, and a QMenu cannot be driven from a test.
	 *
	 * @throws std::runtime_error if a set already stands at that key -- it is authored work, and an
	 *         empty one over it would be the loss this layout exists to prevent -- or for anything
	 *         `assetlib::blendSetKeyFor` and `AssetStore::Save` refuse.
	 */
	[[nodiscard]] std::string
	CreateEmptyBlendSet(const std::filesystem::path& dataRoot, std::string_view animationsKey);

	/**
	 * The set at `key`, as authored -- clips by name, which is the form every rule in
	 * `blend_edits.h` takes and the form that is written back.
	 *
	 * Read from disk rather than rebuilt from what the acquire resolved: the resolved spaces hold
	 * clip *indices* and carry neither the document's `extraJson` nor its `name`, and a save built
	 * from them would quietly drop both.
	 *
	 * @throws std::runtime_error for what `AssetStore::Load` throws on a missing or unreadable
	 *         container, and for what `validateBlendSet` refuses in one already on disk.
	 */
	[[nodiscard]] assetlib::BlendSet
	LoadBlendSet(const std::filesystem::path& dataRoot, std::string_view key);

	/**
	 * Writes `set` back to `key`, over what stands there.
	 *
	 * Over, and not beside: this is the authored document being edited, so the write is the edit.
	 * Unknown keys ride along in `extraJson` because that is what `LoadBlendSet` handed over --
	 * which is the whole reason an edit loads the document rather than rebuilding it.
	 *
	 * @throws std::runtime_error for everything `validateBlendSet` refuses -- an unnamed space, two
	 *         spaces of one name, a space under two samples, a sample naming no clip, parameters
	 *         that are not strictly increasing -- since the codec validates on the way out. A
	 *         refusal here means nothing was written.
	 */
	void
	SaveBlendSet(
		const std::filesystem::path& dataRoot,
		std::string_view             key,
		const assetlib::BlendSet&    set);
}
