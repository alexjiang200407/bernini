#include <assetlib/AssetStore.h>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "fs_util.h"

namespace assetlib
{
	std::vector<std::byte>
	AssetStore::ReadBytes(std::string_view path) const
	{
		return m_Files->Read(path);
	}

	void
	AssetStore::WriteBytes(
		std::string_view           path,
		std::span<const std::byte> bytes,
		std::string_view           what) const
	{
		const std::filesystem::path resolved = ResolveWritePath(path);

		// A key names a location in the data root, not one that already exists: an import aimed at
		// `Derived/Meshes/<folder>/` and a project scaffolded before a category existed both write
		// where no directory is yet.
		createDirectories(resolved.parent_path());
		writeFileBytes(resolved, bytes, what);
	}
}
