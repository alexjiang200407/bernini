#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BVat.h>

namespace game
{
	/** Why a pair's `.bvat` cannot be used as it stands -- or that it can. */
	enum class VatBakeState
	{
		kFresh,       // usable as it stands
		kMissing,     // no `.bvat` for this pair, or one that will not parse
		kStale,       // an input moved since it was baked, or its geometry group is a cache miss
		kOtherClips,  // baked from a different `.banim` than the one asked for
	};

	/**
	 * EnsureVatBaked's rule, asked instead of enforced: what state the pair's bake is in, without
	 * making one. For a caller that must not bake behind the user's back -- the editor's Animation
	 * panel, where a bake is seconds and therefore a decision.
	 *
	 * A read-only store answers `kFresh` for anything it carries, the same trust EnsureVatBaked
	 * extends: a shipped archive is `pack`'s output and the staleness question has no answer worth
	 * asking. What it does not carry is `kMissing`, which is honest but unfixable there.
	 *
	 * Answering costs a full read of the container. Pass `out` to keep it: on `kFresh` that is the
	 * bake itself, so a caller that asks and then loads pays for one read rather than two.
	 */
	[[nodiscard]] VatBakeState
	VatFreshness(
		const assetlib::AssetStore& store,
		std::string_view            meshRelPath,
		std::string_view            animationsRelPath,
		assetlib::BVat*             out = nullptr);

	/**
	 * The `.bvat` for the pair at `meshRelPath` + `animationsRelPath`, fresh: loaded when it is up
	 * to date, otherwise re-baked and written to the store's writable layer. Fresh means the three
	 * input stamps hold (vatIsStale) *and* the container was baked from the `.banim` asked for --
	 * one baked from a different clip set is stale, never silently returned.
	 *
	 * Reads through the store, so a rig that resolves out of an archive bakes correctly and the
	 * result lands in the overlay, which may not have held it before.
	 *
	 * **A store with nowhere to write is trusted instead.** A shipped mount is an archive `pack`
	 * baked every `.bvat` into, so what it carries is correct by construction and the staleness
	 * question has no answer worth asking. The clip-set check still holds even there: loading the
	 * wrong clips is worse than refusing. One such store does not carry cannot be made, and throws.
	 *
	 * Pure assetlib -- no bgl, safe off the render thread. The step AcquireVatMesh runs before it
	 * uploads, exposed so a caller can pay the seconds of CPU skinning on a worker and acquire
	 * afterwards.
	 *
	 * @throws std::runtime_error if an input cannot be read, if the bake refuses it, or if the
	 *         store is read-only and does not carry a `.bvat` for this pair.
	 */
	[[nodiscard]] assetlib::BVat
	EnsureVatBaked(
		const assetlib::AssetStore& store,
		std::string_view            meshRelPath,
		std::string_view            animationsRelPath);
}
