#include <array>
#include <assetlib/envmap.h>

#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/import_document.h>
#include <assetlib/project_layout.h>

#include <assetlib/image_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

#include "baked_name.h"
#include "env_bake.h"
#include "env_parts.h"
#include "env_produce.h"
#include "fs_util.h"
#include <core/file/IFileSystem.h>

#include "mounted_io.h"
#include "texture_encoding.h"

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
		 * The LDR role for a sky or prefilter whose every value fits in [0, 1], the HDR role otherwise.
		 * The irradiance map is one small mip and stays HDR whatever it holds; a face that is not a
		 * multiple of the 4x4 block is refused by D3D12 as a block-compressed top level.
		 */
		TextureRole
		roleFor(const ImageData& cube, std::string_view group)
		{
			if (group == c_IrradianceGroup || cube.width % 4 != 0 || cube.height % 4 != 0)
				return TextureRole::kEnvironmentHdr;
			return isLowDynamicRange(cube) ? TextureRole::kEnvironmentLdr :
			                                 TextureRole::kEnvironmentHdr;
		}

		void
		writeEnvMap(
			const ImageData&             cube,
			const TextureEncoding&       encoding,
			const std::filesystem::path& target)
		{
			if (encoding.compression == Ktx2Compression::kNone)
				writeKTX2(packRgb9e5(cube), target, false, Ktx2Compression::kNone);
			else
				writeKTX2(quantizeSrgb8(cube), target, true, encoding.compression);
		}

		bool
		hasBytes(const std::filesystem::path& path)
		{
			std::error_code ec;
			const auto      size = std::filesystem::file_size(path, ec);
			return !ec && size > 0;
		}

		/**
		 * Bakes one route: encodes `image` into the content-addressed target unless a map is already
		 * there under that name -- which, since the name covers everything that produced it, is this
		 * map. Returns the updated route; the caller assigns it, so a failure part-way leaves the
		 * asset untouched.
		 */
		EnvMapRoute
		bakeRoute(
			const EnvMapRoute&           route,
			const ImageData&             image,
			std::string_view             group,
			const SourceStamp&           stamp,
			uint64_t                     parametersHash,
			const std::filesystem::path& dataRoot)
		{
			const TextureEncoding encoding = textureEncoding(roleFor(image, group));
			const std::string     name     = bakedMapContentName(
				group,
				std::format(
					"{}|{}|{}|{:016x}{:016x}|{:016x}|{:016x}",
					group,
					encoding.tag,
					route.source,
					stamp.size,
					stamp.hash,
					parametersHash,
					c_EnvSourceBakeToken));

			const std::string file = name + std::string(c_TextureExtension);

			const std::filesystem::path outDir = dataRoot / c_BakedTexturesDirectoryName;
			createDirectories(outDir);
			const std::filesystem::path target = outDir / file;

			if (!hasBytes(target))
				writeEnvMap(image, encoding, target);

			EnvMapRoute baked = route;
			baked.baked =
				(std::filesystem::path(c_BakedTexturesDirectoryName) / file).generic_string();
			baked.stamp = stamp;
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

		/**
		 * The parameters `sourceKey` was imported at, from the `.bimport` beside it: what a route's
		 * source alone cannot say.
		 *
		 * @throws std::runtime_error if the document is absent, will not read, or records no
		 *         environment.
		 */
		EnvironmentImportParameters
		importedParameters(const AssetStore& store, const std::string& sourceKey)
		{
			const std::string documentKey = importDocumentKeyFor(sourceKey);
			core::throw_runtime_error_if(
				!store.Exists(documentKey),
				"'{}' has no import document beside it, so what it was imported at is unknowable; "
				"re-import it",
				sourceKey);

			const ImportDocument document = loadImportDocument(store.GetFiles(), documentKey);
			core::throw_runtime_error_if(
				!document.environment,
				"'{}' records no environment parameters",
				documentKey);
			return *document.environment;
		}
	}

	void
	bakeSkyFrom(
		BSky&                        sky,
		const ImageData&             chain,
		const SourceStamp&           stamp,
		uint64_t                     parametersHash,
		const std::filesystem::path& dataRoot)
	{
		sky.sky = bakeRoute(sky.sky, chain, c_SkyGroup, stamp, parametersHash, dataRoot);
	}

	void
	bakeEnvLightingFrom(
		BEnvLighting&                lighting,
		const ImageData&             prefilter,
		const ImageData&             irradiance,
		const SourceStamp&           stamp,
		uint64_t                     parametersHash,
		const std::filesystem::path& dataRoot)
	{
		const EnvMapRoute bakedPrefilter = bakeRoute(
			lighting.prefilter,
			prefilter,
			c_PrefilterGroup,
			stamp,
			parametersHash,
			dataRoot);
		const EnvMapRoute bakedIrradiance = bakeRoute(
			lighting.irradiance,
			irradiance,
			c_IrradianceGroup,
			stamp,
			parametersHash,
			dataRoot);

		lighting.prefilter  = bakedPrefilter;
		lighting.irradiance = bakedIrradiance;
		lighting.exposure   = exposureFor(irradiance);
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
		// A stale map is still drawn: the source is an image to convolve, not one to sample, and
		// minutes of convolution do not belong in a load.
		if (!route.baked.empty() && stampOf(fileSystem, route.baked).size != 0)
			return route.baked;

		core::throw_runtime_error(
			"assetlib::envMapToDraw: the baked map '{}' is not on disk; `assetlib_cli migrate` "
			"bakes "
			"it from '{}'",
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
		core::throw_runtime_error_if(
			sky.sky.source.empty(),
			"assetlib::bakeSky: nothing is routed");

		const EnvironmentImportParameters parameters = importedParameters(*this, sky.sky.source);
		const SourceStamp                 stamp      = StampOf(sky.sky.source);

		throwIfCancelled(cancel);
		auto            input = EnvironmentInput(GetDataRoot() / sky.sky.source);
		const ImageData chain = skyChainOf(input, parameters, 0);

		throwIfCancelled(cancel);
		bakeSkyFrom(
			sky,
			chain,
			stamp,
			partParametersHashOf(parameters, EnvironmentPart::kSky),
			GetDataRoot());
	}

	void
	AssetStore::BakeEnvLighting(BEnvLighting& lighting, const CancelToken& cancel) const
	{
		core::throw_runtime_error_if(
			lighting.prefilter.source.empty() ||
				lighting.prefilter.source != lighting.irradiance.source,
			"assetlib::bakeEnvLighting: both maps must route one source; they are convolutions of "
			"one radiance and cannot be baked apart");

		const std::string&                source     = lighting.prefilter.source;
		const EnvironmentImportParameters parameters = importedParameters(*this, source);
		const SourceStamp                 stamp      = StampOf(source);

		throwIfCancelled(cancel);
		auto               input = EnvironmentInput(GetDataRoot() / source);
		const LightingMaps maps  = lightingMapsOf(input, parameters, 0);

		throwIfCancelled(cancel);
		bakeEnvLightingFrom(
			lighting,
			maps.prefilter,
			maps.irradiance,
			stamp,
			partParametersHashOf(parameters, EnvironmentPart::kLighting),
			GetDataRoot());
	}
}
