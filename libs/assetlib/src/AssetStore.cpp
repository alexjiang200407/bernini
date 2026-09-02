#include <assetlib/AssetStore.h>
#include <assetlib/codecs.h>  // requireInsideDataRoot

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

#include "ref_paths.h"

namespace assetlib
{
	AssetStore::AssetStore(std::filesystem::path dataRoot) :
		m_DataRoot(std::move(dataRoot)),
		m_Files(std::make_shared<const core::file::LooseFileSystem>(m_DataRoot))
	{
		// Here and not at each use: a mount over a directory that is not there enumerates empty
		// rather than failing, so a mistyped root would otherwise read as a project with nothing in
		// it -- a scan reporting no assets, a prune reporting nothing to sweep.
		if (!std::filesystem::is_directory(m_DataRoot))
			core::throw_runtime_error(
				"assetlib::AssetStore: '{}' is not a directory",
				m_DataRoot.string());
	}

	AssetStore::AssetStore(
		std::filesystem::path                          dataRoot,
		std::shared_ptr<const core::file::IFileSystem> files) :
		m_DataRoot(std::move(dataRoot)), m_Files(std::move(files))
	{
		if (!m_Files)
			core::throw_runtime_error("assetlib::AssetStore: a source must have somewhere to read");
	}

	std::filesystem::path
	AssetStore::ResolveWritePath(std::string_view path) const
	{
		const std::string key = normalizeRef(path);
		requireInsideDataRoot("assetlib::AssetStore::ResolveWritePath", key);
		return m_DataRoot / key;
	}

	std::string
	AssetStore::KeyFor(const std::filesystem::path& path) const
	{
		std::error_code             ec;
		const std::filesystem::path relative = std::filesystem::relative(path, m_DataRoot, ec);
		core::throw_runtime_error_if(
			ec || relative.empty() || *relative.begin() == "..",
			"assetlib::AssetStore::KeyFor: '{}' is not inside the data root '{}'",
			path.string(),
			m_DataRoot.string());

		// generic_string, not string: a key is `/`-separated, and on Windows the native spelling
		// is not -- a `\`-separated key resolves loose and misses packed. See STYLE.md's Paths.
		return relative.generic_string();
	}
}
