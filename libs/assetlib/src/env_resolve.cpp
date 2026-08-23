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
		resolved.skyRotationY = env.skyRotationY;

		if (!env.sky.empty())
		{
			const BSky sky       = load<BSky>(fileSystem, env.sky);
			resolved.maps.skybox = loadRoute(fileSystem, sky.sky);

			// The document records the request; the baked map decides what can be served.
			resolved.skyMipLevel = std::min(env.skyMipLevel, resolved.maps.skybox.mipLevels - 1);
		}

		if (!env.lighting.empty())
		{
			const BEnvLighting lighting = load<BEnvLighting>(fileSystem, env.lighting);
			resolved.maps.prefilter     = loadRoute(fileSystem, lighting.prefilter);
			resolved.maps.irradiance    = loadRoute(fileSystem, lighting.irradiance);

			resolved.maps.exposure = effectiveExposure(env, lighting);
		}

		return resolved;
	}
}
