#include <assetlib/AssetStore.h>
#include <assetlib/envmap.h>

#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/SourceStamp.h>
#include <filesystem>
#include <string>
#include <string_view>

#include "mounted_io.h"

namespace assetlib
{
	SourceStamp
	AssetStore::StampOf(std::string_view path) const
	{
		return stampOf(*m_Files, path);
	}

	bool
	AssetStore::BakeIsStale(const BMaterial& material) const
	{
		return bakeIsStale(material, *m_Files);
	}

	bool
	AssetStore::DrawsLoose(const BMaterial& material) const
	{
		return drawsLoose(material, *m_Files);
	}

	bool
	AssetStore::IsSkyBakeStale(const BSky& sky) const
	{
		return isSkyBakeStale(sky, *m_Files);
	}

	bool
	AssetStore::IsEnvLightingBakeStale(const BEnvLighting& lighting) const
	{
		return isEnvLightingBakeStale(lighting, *m_Files);
	}

	const std::string&
	AssetStore::EnvMapToDraw(const EnvMapRoute& route) const
	{
		return envMapToDraw(route, *m_Files);
	}

	ResolvedEnvironment
	AssetStore::ResolveEnvironment(const std::filesystem::path& benvPath) const
	{
		return resolveEnvironment(benvPath, *m_Files);
	}
}
