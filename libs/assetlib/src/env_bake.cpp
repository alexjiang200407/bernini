#include <assetlib/env_bake.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/envmap_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>

#include "baked_name.h"
#include "fs_util.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_SkyGroup        = "sky";
		constexpr std::string_view c_PrefilterGroup  = "prefilter";
		constexpr std::string_view c_IrradianceGroup = "irradiance";

		constexpr std::array<std::string_view, 3> c_EnvGroups = {
			c_SkyGroup,
			c_PrefilterGroup,
			c_IrradianceGroup,
		};

		ImageData
		loadFloatCube(const std::filesystem::path& dataRoot, const std::string& source)
		{
			ImageData image = loadKTX2(dataRoot / source);
			if (image.vkFormat != VkFormat::R32G32B32A32_SFLOAT || !image.isCubemap)
				throw std::runtime_error(
					"assetlib::bakeSky/bakeEnvLighting: source '" + source +
					"' is not a float cube map; environment sources are the R32G32B32A32_SFLOAT "
					"intermediates the import writes into textures_src/");
			return image;
		}

		/**
		 * Bakes one route: packs its already-loaded float cube as RGB9E5 into the content-addressed
		 * target, unless the target is already newer than the source. Returns the updated route;
		 * the caller assigns it, so a failure part-way leaves the asset untouched.
		 */
		EnvMapRoute
		bakeRoute(
			const EnvMapRoute& route,
			const ImageData&   source,
			std::string_view   group,
			const EnvBakeDesc& desc)
		{
			const std::string name =
				bakedMapFileName(group, std::string(group) + '|' + route.source);

			const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
			createDirectories(outDir);
			const std::filesystem::path target = outDir / name;

			const SourceStamp sourceStamp = stampOf(desc.dataRoot / route.source);
			const SourceStamp targetStamp = stampOf(target);
			if (targetStamp.size == 0 || sourceStamp.mtime > targetStamp.mtime)
				writeKTX2(packRgb9e5(source), target, false, Ktx2Compression::kNone);

			EnvMapRoute baked = route;
			baked.baked       = (desc.textureDir / name).generic_string();
			baked.stamp       = sourceStamp;
			return baked;
		}

		bool
		routeIsStale(const EnvMapRoute& route, const std::filesystem::path& dataRoot)
		{
			if (route.source.empty())
				return false;

			// A zeroed stamp means never baked; stampOf zeroes a missing file. Neither can equal a
			// live source's stamp, so both fall out of this comparison as stale.
			if (stampOf(dataRoot / route.source) != route.stamp)
				return true;

			// Named is not the same as present: a map deleted since the bake leaves the route
			// pointing at a file there is nothing to sample. A bake cannot claim what it cannot
			// produce, so that is stale and not up to date.
			return route.baked.empty() || stampOf(dataRoot / route.baked).size == 0;
		}
	}

	void
	bakeSky(BSky& sky, const EnvBakeDesc& desc, const CancelToken& cancel)
	{
		if (sky.sky.source.empty())
			throw std::runtime_error("assetlib::bakeSky: nothing is routed");

		throwIfCancelled(cancel);
		sky.sky =
			bakeRoute(sky.sky, loadFloatCube(desc.dataRoot, sky.sky.source), c_SkyGroup, desc);
	}

	void
	bakeEnvLighting(BEnvLighting& lighting, const EnvBakeDesc& desc, const CancelToken& cancel)
	{
		if (lighting.prefilter.source.empty() || lighting.irradiance.source.empty())
			throw std::runtime_error(
				"assetlib::bakeEnvLighting: both maps must be routed; they are convolutions of one "
				"radiance and cannot be baked apart");

		throwIfCancelled(cancel);
		const ImageData   prefilterSrc = loadFloatCube(desc.dataRoot, lighting.prefilter.source);
		const EnvMapRoute prefilter =
			bakeRoute(lighting.prefilter, prefilterSrc, c_PrefilterGroup, desc);

		throwIfCancelled(cancel);
		const ImageData   irradianceSrc = loadFloatCube(desc.dataRoot, lighting.irradiance.source);
		const EnvMapRoute irradiance =
			bakeRoute(lighting.irradiance, irradianceSrc, c_IrradianceGroup, desc);

		lighting.prefilter  = prefilter;
		lighting.irradiance = irradiance;
		lighting.exposure   = exposureFor(irradianceSrc);
	}

	bool
	skyBakeIsStale(const BSky& sky, const std::filesystem::path& dataRoot)
	{
		return routeIsStale(sky.sky, dataRoot);
	}

	bool
	envLightingBakeIsStale(const BEnvLighting& lighting, const std::filesystem::path& dataRoot)
	{
		return routeIsStale(lighting.prefilter, dataRoot) ||
		       routeIsStale(lighting.irradiance, dataRoot);
	}

	bool
	isBakedEnvMapName(std::string_view fileName) noexcept
	{
		return isBakedNameAmong(fileName, c_EnvGroups);
	}
}
