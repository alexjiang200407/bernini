#include <assetlib/AssetStore.h>

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
		writeFileBytes(ResolveWritePath(path), bytes, what);
	}
}
