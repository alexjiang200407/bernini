#include <algorithm>
#include <assetlib/codecs.h>
#include <assetlib/envmap.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>

#include "mounted_io.h"
#include <core/file/file.h>
#include <filesystem>

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
		// A host path by contract: resolveEnvironment takes the `.benv` where it sits and keys
		// only the chain below it. See AssetStore::ResolveEnvironment.
		const BEnv env =
			AssetCodec<BEnv>::Deserialize(core::file::read_file_bytes(benvPath.string()));

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
