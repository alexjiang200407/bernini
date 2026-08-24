#include "Windows/MaterialEditor/CachedMaterial.h"

#include <QDebug>

#include "util/asset_paths.h"
#include <assetlib/AssetStore.h>

const assetlib::BMaterial*
CachedMaterial::Get(const std::filesystem::path& dataRoot, const QString& path)
{
	if (path.isEmpty())
	{
		Forget();
		return nullptr;
	}

	// A path with no file behind it is the ordinary Save As case, not an error: reporting it per
	// submesh selection would be the loudest thing in the log.
	const qint64 stamp = editor::FileStamp(path);
	if (stamp == 0)
	{
		Forget();
		return nullptr;
	}

	if (m_Material && m_Path == path && m_Stamp == stamp)
		return &*m_Material;

	try
	{
		const assetlib::AssetStore store(dataRoot);
		m_Material = store.Load<assetlib::BMaterial>(
			store.KeyFor(std::filesystem::path(path.toStdWString())));
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialEditor: could not read the material: %s", e.what());
		Forget();
		return nullptr;
	}

	m_Path  = path;
	m_Stamp = stamp;
	return &*m_Material;
}

void
CachedMaterial::Forget() noexcept
{
	m_Material.reset();
	m_Path.clear();
	m_Stamp = 0;
}
