#include <array>
#include <assetlib/container_info.h>
#include <assetlib/envmap.h>

#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "baked_name.h"
#include "fs_util.h"
#include <assetlib_structs/VkFormat.h>
#include <core/file/IFileSystem.h>

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
					"intermediates the import writes into Derived/SourceTextures/");
			return image;
		}

		enum class EnvMapEncoding : uint8_t
		{
			kRgb9e5,
			kBc7Srgb,
		};

		std::string_view
		encodingTag(EnvMapEncoding encoding) noexcept
		{
			switch (encoding)
			{
			case EnvMapEncoding::kRgb9e5:
				return "rgb9e5";
			case EnvMapEncoding::kBc7Srgb:
				return "bc7srgb";
			}
			return {};
		}

		bool
		isLowDynamicRange(const ImageData& cube)
		{
			const auto*  texels = reinterpret_cast<const float*>(cube.pixels.data());
			const size_t count  = cube.pixels.size() / (sizeof(float) * 4);
			for (size_t t = 0; t < count; ++t)
				for (size_t c = 0; c < 3; ++c)
					if (!(texels[t * 4 + c] <= 1.0f))
						return false;
			return true;
		}

		/**
		 * BC7 for a sky or prefilter whose every value fits it, RGB9E5 otherwise. The irradiance map
		 * is one small mip and stays RGB9E5 whatever it holds; a face that is not a multiple of the
		 * 4x4 block is refused by D3D12 as a block-compressed top level.
		 */
		EnvMapEncoding
		encodingFor(const ImageData& cube, std::string_view group)
		{
			if (group == c_IrradianceGroup || cube.width % 4 != 0 || cube.height % 4 != 0)
				return EnvMapEncoding::kRgb9e5;
			return isLowDynamicRange(cube) ? EnvMapEncoding::kBc7Srgb : EnvMapEncoding::kRgb9e5;
		}

		void
		writeEnvMap(
			const ImageData&             cube,
			EnvMapEncoding               encoding,
			const std::filesystem::path& target)
		{
			switch (encoding)
			{
			case EnvMapEncoding::kRgb9e5:
				writeKTX2(packRgb9e5(cube), target, false, Ktx2Compression::kNone);
				return;
			case EnvMapEncoding::kBc7Srgb:
				writeKTX2(quantizeSrgb8(cube), target, true, Ktx2Compression::kBC7_RGBA);
				return;
			}
		}

		/**
		 * Bakes one route: encodes its already-loaded float cube into the content-addressed target,
		 * unless the target is already newer than the source. Returns the updated route; the caller
		 * assigns it, so a failure part-way leaves the asset untouched.
		 */
		EnvMapRoute
		bakeRoute(
			const EnvMapRoute& route,
			const ImageData&   source,
			std::string_view   group,
			const BakeDesc&    desc)
		{
			// The encoding is in the name so a re-bake that changes it never reuses the old file,
			// which the mtime test below would otherwise take as current.
			const EnvMapEncoding encoding = encodingFor(source, group);
			const std::string    name     = bakedMapFileName(
				group,
				std::format("{}|{}|{}", group, encodingTag(encoding), route.source));

			const std::filesystem::path outDir = desc.dataRoot / desc.textureDir;
			createDirectories(outDir);
			const std::filesystem::path target = outDir / name;

			// Mtime ordering rather than a stamp comparison: this name covers the group and the
			// source's path but not its content, so the target records nothing about what produced it
			// -- and two environments sharing a source share the target, so a stamp test would
			// re-encode what the other just wrote. (A material's maps are named for their content and
			// so need no ordering at all; this one is not, yet.)
			const std::filesystem::path sourcePath  = desc.dataRoot / route.source;
			const SourceStamp           sourceStamp = stampOf(sourcePath);

			const std::optional<std::filesystem::file_time_type> written = mtimeOf(target);
			const std::optional<std::filesystem::file_time_type> touched = mtimeOf(sourcePath);

			if (stampOf(target).size == 0 || !written || !touched || *touched > *written)
				writeEnvMap(source, encoding, target);

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
		bakeSky(
			sky,
			{ .dataRoot = m_DataRoot, .textureDir = c_BakedTexturesDirectoryName },
			cancel);
	}

	void
	AssetStore::BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel) const
	{
		bakeEnvLighting(
			lighting,
			{ .dataRoot = m_DataRoot, .textureDir = c_BakedTexturesDirectoryName },
			cancel);
	}
}
