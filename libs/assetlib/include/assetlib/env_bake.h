#pragma once
#include <assetlib/cancel.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct BSky;
	struct BEnvLighting;
	struct EnvMapRoute;

	/**
	 * Where an environment bake reads and writes. Mirrors MaterialBakeDesc: every path a `.bsky` or
	 * `.benvl` stores is relative to `dataRoot`, and baked maps land in `dataRoot / textureDir`.
	 */
	struct EnvBakeDesc
	{
		std::filesystem::path dataRoot;
		std::filesystem::path textureDir = "Textures";
	};

	/**
	 * Compiles a sky's routed source -- a float radiance cube under `textures_src/` -- into the
	 * shipping RGB9E5 `.ktx2` under `desc.textureDir`, updating `sky`'s route and stamp in place.
	 *
	 * **Baked maps are shared, not owned**, exactly as bakeMaterial's are: the name is content
	 * addressed from the route, so two skies routing the same source share one baked file.
	 *
	 * @param cancel Polled before the encode that dominates the cost. A cancelled bake leaves `sky`
	 *        as it was, but a map already written stays on disk for the next bake to reuse.
	 * @throws std::runtime_error if nothing is routed, or the source is missing, not a float cube
	 *         map, or cannot be written.
	 * @throws Cancelled if `cancel` is signalled.
	 */
	void
	bakeSky(BSky& sky, const EnvBakeDesc& desc, const CancelToken& cancel = {});

	/**
	 * bakeSky for the lighting pair: both convolutions are compiled to RGB9E5, and the exposure is
	 * re-derived from the irradiance source -- it is a property of the maps, so it must move whenever
	 * they do.
	 *
	 * Both routes must be routed. The maps are halves of one thing, and baking one against a stale
	 * other would produce the quiet disagreement the pair exists to prevent.
	 *
	 * @throws std::runtime_error / Cancelled as bakeSky.
	 */
	void
	bakeEnvLighting(
		BEnvLighting&      lighting,
		const EnvBakeDesc& desc,
		const CancelToken& cancel = {});

	/**
	 * Whether `sky`'s baked map no longer reflects the source its route names. False when unrouted --
	 * there is no source to have drifted from -- and true when the source changed, went missing, was
	 * never baked, or when the map the route names is no longer on disk.
	 *
	 * That last case is the same rule the material triplet follows: a bake cannot claim a map that is
	 * not there to sample, so a deleted one reads as stale rather than as up to date.
	 */
	[[nodiscard]] bool
	isSkyBakeStale(const BSky& sky, const core::file::IFileSystem& fileSystem);

	/** isSkyBakeStale over the lighting pair: stale when either route is. */
	[[nodiscard]] bool
	isEnvLightingBakeStale(const BEnvLighting& lighting, const core::file::IFileSystem& fileSystem);

	/**
	 * The map a consumer draws for `route`, data-root relative: the baked RGB9E5 while that is on
	 * disk and current, and the float source it was compiled from while it is not.
	 *
	 * The rule a material follows -- see drawsLoose. A stale or never-written bake falls back to
	 * what it was compiled from, but only a source that is still there can be sampled, so a route
	 * whose source has gone keeps its baked map. Two representations of one image: the fallback
	 * costs memory and skips the shipping compile, it does not change what is drawn.
	 *
	 * @throws std::runtime_error when neither is on disk, which is the only case a consumer cannot
	 *         draw at all.
	 */
	[[nodiscard]] const std::string&
	envMapToDraw(const EnvMapRoute& route, const core::file::IFileSystem& fileSystem);

	/**
	 * Whether `fileName` is a name bakeSky or bakeEnvLighting could have written:
	 * `<group>_<16 hex digits>.ktx2` for an environment group. The counterpart of isBakedMapName,
	 * disjoint from it by group name, and what lets the texture prune consider environment maps
	 * without mistaking them for a material's.
	 */
	[[nodiscard]] bool
	isBakedEnvMapName(std::string_view fileName) noexcept;
}
