#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/BVat.h>

namespace game
{
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
