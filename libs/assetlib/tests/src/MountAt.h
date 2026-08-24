#pragma once
#include <assetlib/AssetCodec.h>
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

/**
 * Save or load a container at an absolute path, for a round trip that owns the whole path rather
 * than a project's key.
 *
 * The store is over the file's own directory, so the key is just its name. That is the honest
 * reading of what these cases are: they are testing the codec, not a project's layout, and the
 * directory they picked is the only root there is.
 */
template <assetlib::AssetCodecFor T>
void
SaveAt(const T& value, const std::filesystem::path& file)
{
	StoreAt(file.parent_path()).Save(value, file.filename().generic_string());
}

template <assetlib::AssetCodecFor T>
[[nodiscard]] T
LoadAt(const std::filesystem::path& file)
{
	return StoreAt(file.parent_path()).template Load<T>(file.filename().generic_string());
}
