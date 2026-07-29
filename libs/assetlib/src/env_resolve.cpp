#include <assetlib/env_resolve.h>

#include <assetlib/benvl_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>

namespace assetlib
{
	namespace
	{
		ImageData
		loadBaked(
			const std::filesystem::path& dataRoot,
			const std::string&           baked,
			const std::string&           asset)
		{
			if (baked.empty())
				throw std::runtime_error(
					"assetlib::resolveEnvironment: '" + asset +
					"' has never been baked; bake it before resolving");
			return loadKTX2(dataRoot / baked);
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
			resolved.maps.skybox  = loadBaked(dataRoot, sky.sky.baked, env.sky);
			resolved.skyMipLevel  = sky.mipLevel;
			resolved.skyRotationY = sky.rotationY;
		}

		if (!env.lighting.empty())
		{
			const BEnvLighting lighting = loadEnvLighting(dataRoot / env.lighting);
			resolved.maps.prefilter  = loadBaked(dataRoot, lighting.prefilter.baked, env.lighting);
			resolved.maps.irradiance = loadBaked(dataRoot, lighting.irradiance.baked, env.lighting);
			resolved.maps.exposure   = lighting.exposure;
		}

		return resolved;
	}
}
