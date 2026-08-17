#include <assetlib/AssetStore.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

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

}
