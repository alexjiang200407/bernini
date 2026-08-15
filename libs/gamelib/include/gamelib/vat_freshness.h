#pragma once
#include <assetlib_structs/BVat.h>

namespace game
{
	/**
	 * The `.bvat` beside the mesh at `meshRelPath`, fresh: loaded when it is up to date, otherwise
	 * re-baked from `meshRelPath` + `animationsRelPath` and rewritten in place. Fresh means the
	 * three input stamps hold (vatIsStale) *and* the container was baked from the `.banim` asked
	 * for -- one baked from a different clip set is stale, never silently returned.
	 *
	 * Pure assetlib -- no bgl, safe off the render thread.
	 *
	 * TODO: take feat/archive's IFileSystem instead of a raw data root once it lands -- the
	 * archive branch moves vatIsStale onto that seam, and a bake inside a .bpak has no path. The step AcquireVatMesh runs before it
	 * uploads, exposed so a caller can pay the seconds of CPU skinning on a worker and acquire
	 * afterwards.
	 *
	 * @throws std::runtime_error if an input cannot be read or the bake refuses it.
	 */
	[[nodiscard]] assetlib::BVat
	EnsureVatBaked(
		const std::filesystem::path& dataRoot,
		std::string_view             meshRelPath,
		std::string_view             animationsRelPath);
}
