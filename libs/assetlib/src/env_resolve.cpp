#include <assetlib/env_resolve.h>

#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		ImageData
		loadRoute(const core::file::IFileSystem& fileSystem, const EnvMapRoute& route)
		{
			return loadKTX2(fileSystem, envMapToDraw(route, fileSystem));
		}
	}

	ResolvedEnvironment
	resolveEnvironment(
		const std::filesystem::path&   benvPath,
		const core::file::IFileSystem& fileSystem)
	{
		const BEnv env = loadEnv(benvPath);

		ResolvedEnvironment resolved;

		if (!env.sky.empty())
		{
			const BSky sky        = loadSky(fileSystem, env.sky);
			resolved.maps.skybox  = loadRoute(fileSystem, sky.sky);
			resolved.skyMipLevel  = sky.mipLevel;
			resolved.skyRotationY = sky.rotationY;
		}

		if (!env.lighting.empty())
		{
			const BEnvLighting lighting = loadEnvLighting(fileSystem, env.lighting);
			resolved.maps.prefilter     = loadRoute(fileSystem, lighting.prefilter);
			resolved.maps.irradiance    = loadRoute(fileSystem, lighting.irradiance);
			resolved.maps.exposure      = lighting.EffectiveExposure();
		}

		return resolved;
	}
}
