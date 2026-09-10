#pragma once

#include <assetlib/asset_refs.h>
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
}
