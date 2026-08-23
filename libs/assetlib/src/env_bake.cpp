#include <assetlib/env_bake.h>

#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/envmap_bake.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <core/err/util.h>

#include "baked_name.h"
#include "fs_util.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		// What a bake reads and writes: the store's data root, and the directory baked maps land in
		// relative to it. Not public -- a caller names the store, which already holds the root.
		struct BakeDesc
		{
			std::filesystem::path dataRoot;
			std::filesystem::path textureDir;
		};
	}

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
			const BakeDesc&    desc)
		{
			const std::string name =
				bakedMapFileName(group, std::string(group) + '|' + route.source);

			const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
			createDirectories(outDir);
			const std::filesystem::path target = outDir / name;

			// Mtime ordering rather than a stamp comparison, for the reason material_bake's isUpToDate
			// gives: the target records nothing about what produced it, and two environments sharing a
			// source share the target, so a stamp test would re-encode what the other just wrote.
			const std::filesystem::path sourcePath  = desc.dataRoot / route.source;
			const SourceStamp           sourceStamp = stampOf(sourcePath);

			const std::optional<std::filesystem::file_time_type> written = mtimeOf(target);
			const std::optional<std::filesystem::file_time_type> touched = mtimeOf(sourcePath);

			if (stampOf(target).size == 0 || !written || !touched || *touched > *written)
				writeKTX2(packRgb9e5(source), target, false, Ktx2Compression::kNone);

			EnvMapRoute baked = route;
			baked.baked       = (desc.textureDir / name).generic_string();
			baked.stamp       = sourceStamp;
			return baked;
		}

		bool
		routeIsStale(const EnvMapRoute& route, const core::file::IFileSystem& fileSystem)
		{
			if (route.source.empty())
				return false;

			// A zeroed stamp means never baked; stampOf zeroes a missing file. Neither can equal a
			// live source's stamp, so both fall out of this comparison as stale.
			if (stampOf(fileSystem, route.source) != route.stamp)
				return true;

			// Named is not the same as present: a map deleted since the bake leaves the route
			// pointing at a file there is nothing to sample. A bake cannot claim what it cannot
			// produce, so that is stale and not up to date.
			return route.baked.empty() || stampOf(fileSystem, route.baked).size == 0;
		}
	}

	static void
	bakeSky(BSky& sky, const BakeDesc& desc, const CancelToken& cancel)
	{
		if (sky.sky.source.empty())
			throw std::runtime_error("assetlib::bakeSky: nothing is routed");

		throwIfCancelled(cancel);
		sky.sky =
			bakeRoute(sky.sky, loadFloatCube(desc.dataRoot, sky.sky.source), c_SkyGroup, desc);
	}

	static void
	bakeEnvLighting(BEnvLighting& lighting, const BakeDesc& desc, const CancelToken& cancel)
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

		lighting.exposure = exposureFor(irradianceSrc);
	}

	bool
	isSkyBakeStale(const BSky& sky, const core::file::IFileSystem& fileSystem)
	{
		return routeIsStale(sky.sky, fileSystem);
	}

	bool
	isEnvLightingBakeStale(const BEnvLighting& lighting, const core::file::IFileSystem& fileSystem)
	{
		return routeIsStale(lighting.prefilter, fileSystem) ||
		       routeIsStale(lighting.irradiance, fileSystem);
	}

	const std::string&
	envMapToDraw(const EnvMapRoute& route, const core::file::IFileSystem& fileSystem)
	{
		const bool bakedOnDisk = !route.baked.empty() && stampOf(fileSystem, route.baked).size != 0;

		if (bakedOnDisk && !routeIsStale(route, fileSystem))
			return route.baked;

		if (!route.source.empty() && stampOf(fileSystem, route.source).size != 0)
			return route.source;

		if (bakedOnDisk)
			return route.baked;

		core::throw_runtime_error(
			"assetlib::envMapToDraw: neither the baked map '{}' nor the source '{}' is on disk; "
			"bake the environment, or restore its source",
			route.baked,
			route.source);
	}

	bool
	isBakedEnvMapName(std::string_view fileName) noexcept
	{
		return isBakedNameAmong(fileName, c_EnvGroups);
	}

	void
	AssetStore::BakeSky(BSky& sky, const CancelToken& cancel) const
	{
		BakeSky(sky, c_TexturesDirectoryName, cancel);
	}

	void
	AssetStore::BakeSky(BSky& sky, std::string_view textureDir, const CancelToken& cancel) const
	{
		bakeSky(sky, { .dataRoot = m_DataRoot, .textureDir = textureDir }, cancel);
	}

	void
	AssetStore::BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel) const
	{
		BakeEnvLighting(lighting, c_TexturesDirectoryName, cancel);
	}

	void
	AssetStore::BakeEnvLighting(
		BEnvLighting&      lighting,
		std::string_view   textureDir,
		const CancelToken& cancel) const
	{
		bakeEnvLighting(lighting, { .dataRoot = m_DataRoot, .textureDir = textureDir }, cancel);
	}
}
