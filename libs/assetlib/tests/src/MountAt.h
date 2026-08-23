#pragma once
#include <assetlib/AssetStore.h>
#include <core/file/LooseFileSystem.h>

/**
 * A loose mount over `root`, for the predicates that resolve through one.
 *
 * Returned by value despite `LooseFileSystem` being immovable: `return T(args)` initializes the
 * caller's object directly, so a test can write the mount inline at the assertion that asks about
 * that directory rather than naming a local for each.
 */
[[nodiscard]] inline core::file::LooseFileSystem
MountAt(const std::filesystem::path& root)
{
	return core::file::LooseFileSystem(root);
}

/**
 * A store over `root`, for the operations that address a whole project rather than a mount --
 * a bake, a save. The loose constructor is the whole of it: reads and writes both go to `root`.
 */
[[nodiscard]] inline assetlib::AssetStore
StoreAt(const std::filesystem::path& root)
{
	return assetlib::AssetStore(root);
}
