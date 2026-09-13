#include <assetlib/AssetStore.h>
#include <assetlib/envmap.h>

#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/SourceStamp.h>
#include <cstddef>
#include <cstdint>
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
	AssetStore::SurfaceSlotBakeIsStale(const SurfaceTextureBinding& slot) const
	{
		return surfaceSlotBakeIsStale(slot, *m_Files);
	}

	uint32_t
	AssetStore::LooseSurfaceSlots(const BMaterial& material) const
	{
		uint32_t slots = 0;
		for (size_t i = 0; i < material.surface.textures.size() && i < 32; ++i)
		{
			const SurfaceTextureBinding& slot = material.surface.textures[i];
			if (slotIsRouted(slot) && surfaceSlotBakeIsStale(slot, *m_Files))
				slots |= 1u << i;
		}
		return slots;
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
