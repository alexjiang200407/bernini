#include <assetlib/env_resolve.h>

#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>

namespace assetlib
{
	namespace
	{
		ImageData
		loadRoute(const std::filesystem::path& dataRoot, const EnvMapRoute& route)
		{
			return loadKTX2(dataRoot / envMapToDraw(route, dataRoot));
		}
	}

	ResolvedEnvironment
	resolveEnvironment(const std::filesystem::path& benvPath, const std::filesystem::path& dataRoot)
	{
		const BEnv env = loadEnv(benvPath);

		ResolvedEnvironment resolved;

		if (!env.sky.empty())
		{
			const BSky sky        = loadSky(dataRoot / env.sky);
			resolved.maps.skybox  = loadRoute(dataRoot, sky.sky);
			resolved.skyMipLevel  = sky.mipLevel;
			resolved.skyRotationY = sky.rotationY;
		}

		if (!env.lighting.empty())
		{
			const BEnvLighting lighting = loadEnvLighting(dataRoot / env.lighting);
			resolved.maps.prefilter     = loadRoute(dataRoot, lighting.prefilter);
			resolved.maps.irradiance    = loadRoute(dataRoot, lighting.irradiance);
			resolved.maps.exposure      = lighting.EffectiveExposure();
		}

		return resolved;
	}
}
