#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>
#include <assetlib/project_layout.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>

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
 * `<category>/<leaf>`, where `category` is one of project_layout.h's constants.
 *
 * A scratch project is still a project: `AssetStore::Save` refuses a container written outside the
 * half its codec belongs to, so a test names its keys through the same constants the layout is
 * defined by rather than spelling a category that a later rename would strand.
 */
[[nodiscard]] inline std::string
KeyIn(const std::string_view category, const std::string_view leaf)
{
	return std::format("{}/{}", category, leaf);
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
 * The codec directly, not a store: these cases test the codec and not a project's layout, so they
 * address the host as libs/assetlib/CLAUDE.md says a caller addressing the host does. Going
 * through a store instead would be claiming a data root the file is not in, and the layout rule
 * `Save` enforces would rightly refuse the bare filename it would have to pass.
 */
template <assetlib::AssetCodecFor T>
void
SaveAt(const T& value, const std::filesystem::path& file)
{
	std::filesystem::create_directories(file.parent_path());
	core::file::write_atomic(file, assetlib::AssetCodec<T>::Serialize(value));
}

template <assetlib::AssetCodecFor T>
[[nodiscard]] T
LoadAt(const std::filesystem::path& file)
{
	return assetlib::AssetCodec<T>::Deserialize(core::file::read_file_bytes(file));
}
