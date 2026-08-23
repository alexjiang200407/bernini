#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>

/**
 * Save or load a container at an absolute path, for a test that owns the whole path rather than a
 * project's key. The store is over the file's own directory, so the key is just its name.
 *
 * assetlib's suite has the same pair in its MountAt.h; this is the copy for the suites that do not
 * share that header. Both exist because the path-taking io is gone -- a project's asset is written
 * through a store, and a test that named a whole path still has to say which root it meant.
 */
template <assetlib::AssetCodecFor T>
void
SaveAt(const T& value, const std::filesystem::path& file)
{
	assetlib::AssetStore(file.parent_path()).Save(value, file.filename().generic_string());
}

template <assetlib::AssetCodecFor T>
[[nodiscard]] T
LoadAt(const std::filesystem::path& file)
{
	return assetlib::AssetStore(file.parent_path())
	    .template Load<T>(file.filename().generic_string());
}
