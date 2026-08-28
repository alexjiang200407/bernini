#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>
#include <core/file/file.h>

/**
 * Save or load a container at an absolute path, for a test that owns the whole path rather than a
 * project's key.
 *
 * The codec directly, not a store: a directory holding one file is not a project, and `Save`
 * refuses a key naming neither half of one. assetlib's suite has the same pair in its MountAt.h;
 * this is the copy for the suites that do not share that header.
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
