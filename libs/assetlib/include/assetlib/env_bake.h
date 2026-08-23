#pragma once

namespace assetlib
{
	struct EnvMapRoute;

	/**
	 * Whether `fileName` is a name AssetStore::BakeSky or BakeEnvLighting could have written:
	 * `<group>_<16 hex digits>.ktx2` for an environment group. The counterpart of isBakedMapName,
	 * disjoint from it by group name, and what lets the texture prune consider environment maps
	 * without mistaking them for a material's.
	 */
	[[nodiscard]] bool
	isBakedEnvMapName(std::string_view fileName) noexcept;
}
