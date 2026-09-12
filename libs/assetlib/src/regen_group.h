#pragma once
#include <assetlib/import_document.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/SourceRef.h>
#include <core/str/str.h>
#include <mutex>
#include <optional>
#include <string_view>

namespace assetlib
{
	class AssetStore;
	struct AnimationSet;
	struct BMesh;

	/** A source re-imported in memory, with the reference that keys everything derived from it. */
	struct RegeneratedGroup
	{
		imp::BMeshImport              import;
		SourceRef                     ref;
		std::optional<ImportDocument> document;
	};

	/**
	 * `sourceKey` parsed at `document`'s parameters, with textures skipped. Drives both directions:
	 * a stale container re-cooked in memory, and an absent one produced onto disk.
	 *
	 * @throws what `loadFromGltf` throws, and whatever reading the copied source throws.
	 */
	[[nodiscard]] RegeneratedGroup
	importGroup(const AssetStore& store, std::string_view sourceKey, ImportDocument&& document);

	/**
	 * The rigs one walk has resolved, so a rig several containers name is regenerated once rather
	 * than once for each -- regenerating a stale one is a parse of its whole source, and a modular
	 * unit is several meshes and a clip library on a single rig.
	 *
	 * Shareable across the threads a cook walks with.
	 */
	class RigResolver
	{
	public:
		/** @throws what AssetStore::LoadRegenSkeleton throws. */
		[[nodiscard]] Skeleton
		Resolve(const AssetStore& store, std::string_view key);

	private:
		std::mutex                             m_Mutex;
		core::str::unordered_str_map<Skeleton> m_Rigs;
	};

	/**
	 * Re-addresses `container` to the rig it names, where that rig has gained a bone since the
	 * container was cooked -- what AcquireSkinnedMesh does on every load, done once to the bytes so
	 * that no load has to.
	 *
	 * A pairing the remap will not resolve -- a rename, a deletion, a reparent -- is left exactly as
	 * it was: there is no current state to put it at, and the acquire refuses it where it is read.
	 * One naming no rig is a static mesh and has nothing to address.
	 *
	 * @throws what RigResolver::Resolve throws.
	 */
	void
	remapToItsRig(RigResolver& rigs, const AssetStore& store, AnimationSet& clips);

	void
	remapToItsRig(RigResolver& rigs, const AssetStore& store, BMesh& mesh);
}
