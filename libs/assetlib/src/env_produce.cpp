#include "env_produce.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
#include <assetlib/codecs.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib/envmap.h>
#include <assetlib/image_io.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bit>
#include <core/err/util.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "env_bake.h"
#include "env_parts.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		// The sky's defocus chain convolves every level below the first with this many samples.
		constexpr uint32_t c_SkyChainSamples = 256;

		void
		notify(const EnvironmentFileSink& sink, const std::string& key)
		{
			if (sink)
				sink(key);
		}
	}

	EnvironmentInput::EnvironmentInput(const std::filesystem::path& source) :
		m_Equirect(extensionOf(source.generic_string()) == c_EnvSourceHdrExtension)
	{
		m_Input = m_Equirect ? loadRadianceHdr(source) : loadKTX2(source);

		// A baked map imported as a source is a recovery path, not the one to reach for: re-convolving
		// it quantizes a second time.
		if (m_Input.vkFormat == VkFormat::E5B9G9R9_UFLOAT_PACK32)
		{
			spdlog::warn(
				"'{}' is RGB9E5; unpacking it to float. Re-convolving a baked map quantizes twice "
				"-- prefer the source it was baked from",
				source.string());
			m_Input = unpackRgb9e5(m_Input);
		}
	}

	const ImageData&
	EnvironmentInput::CubeAt(uint32_t faceSize)
	{
		if (!m_Equirect)
			return m_Input;

		auto it = m_Cubes.find(faceSize);
		if (it == m_Cubes.end())
			it = m_Cubes.emplace(faceSize, equirectToCube(m_Input, faceSize)).first;
		return it->second;
	}

	ImageData
	skyChainOf(
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads)
	{
		// A chain, never a single blurred mip: the backdrop's defocus is presentation, so it belongs
		// on the `.benv` document where a viewer can change it.
		//
		// Clamped rather than refused: a sky too small for the requested chain is a small sky, not a
		// bad request.
		const auto maxMips =
			static_cast<uint32_t>(std::bit_width(std::max(parameters.skyFaceSize, 1u)));
		return skyChain(
			input.CubeAt(parameters.skyFaceSize),
			parameters.skyFaceSize,
			std::clamp(parameters.skyMips, 1u, maxMips),
			c_SkyChainSamples,
			threads);
	}

	LightingMaps
	lightingMapsOf(
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads)
	{
		const ImageData& radiance = input.CubeAt(lightingProjectionSize(parameters));

		auto prefilterDesc      = PrefilterDesc();
		prefilterDesc.faceSize  = parameters.prefilterFaceSize;
		prefilterDesc.mipLevels = parameters.prefilterMips;
		prefilterDesc.samples   = parameters.prefilterSamples;
		prefilterDesc.threads   = threads;

		auto maps       = LightingMaps();
		maps.prefilter  = prefilterRadiance(radiance, prefilterDesc);
		maps.irradiance = irradianceSh(radiance, parameters.irradianceFaceSize);
		return maps;
	}

	void
	produceSky(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const std::string&                 sourceKey,
		const SourceStamp&                 stamp,
		const std::string&                 containerKey,
		const EnvironmentFileSink&         beforeWrite,
		const EnvironmentFileSink&         afterWrite,
		const CancelToken&                 cancel)
	{
		throwIfCancelled(cancel);
		const ImageData chain = skyChainOf(input, parameters, threads);

		auto bsky       = BSky();
		bsky.name       = std::string(name);
		bsky.sky.source = sourceKey;

		throwIfCancelled(cancel);
		bakeSkyFrom(
			bsky,
			chain,
			stamp,
			partParametersHashOf(parameters, EnvironmentPart::kSky),
			store.GetDataRoot());

		notify(beforeWrite, containerKey);
		store.Save(bsky, containerKey);
		notify(afterWrite, containerKey);
	}

	float
	produceLighting(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const std::string&                 sourceKey,
		const SourceStamp&                 stamp,
		const std::string&                 containerKey,
		const EnvironmentFileSink&         beforeWrite,
		const EnvironmentFileSink&         afterWrite,
		const CancelToken&                 cancel)
	{
		throwIfCancelled(cancel);
		const LightingMaps maps = lightingMapsOf(input, parameters, threads);

		auto lighting              = BEnvLighting();
		lighting.name              = std::string(name);
		lighting.prefilter.source  = sourceKey;
		lighting.irradiance.source = sourceKey;

		throwIfCancelled(cancel);
		bakeEnvLightingFrom(
			lighting,
			maps.prefilter,
			maps.irradiance,
			stamp,
			partParametersHashOf(parameters, EnvironmentPart::kLighting),
			store.GetDataRoot());

		notify(beforeWrite, containerKey);
		store.Save(lighting, containerKey);
		notify(afterWrite, containerKey);
		return lighting.exposure;
	}

	void
	produceEnvironmentOutputs(
		const AssetStore&               store,
		const std::string&              sourceKey,
		const ImportDocument&           document,
		const std::vector<std::string>& wanted,
		const EnvironmentFileSink&      beforeWrite,
		const EnvironmentFileSink&      onWritten,
		const CancelToken&              cancel)
	{
		if (!document.environment)
		{
			core::throw_runtime_error(
				"'{}': its import document records no environment parameters",
				sourceKey);
		}

		auto keys = std::unordered_map<EnvironmentPart, std::string>();
		for (const std::string& output : document.outputs)
		{
			const std::optional<EnvironmentPart> part = environmentPartOf(output);
			if (!part)
			{
				core::throw_runtime_error(
					"'{}': its import document claims '{}', which no environment import writes",
					sourceKey,
					output);
			}
			keys[*part] = output;
		}

		const auto wants = [&](EnvironmentPart part) {
			const auto found = keys.find(part);
			return found != keys.end() && std::ranges::find(wanted, found->second) != wanted.end();
		};

		if (!wants(EnvironmentPart::kSky) && !wants(EnvironmentPart::kLighting))
			return;

		// Taken before the cook, so a source rewritten while a part convolves reads as stale
		// afterwards rather than as the file those pixels came from.
		const SourceStamp stamp = store.StampOf(sourceKey);
		auto              input = EnvironmentInput(store.ResolveWritePath(sourceKey));
		const EnvironmentImportParameters& parameters = *document.environment;

		if (wants(EnvironmentPart::kSky))
		{
			const std::string& key = keys[EnvironmentPart::kSky];
			produceSky(
				store,
				input,
				parameters,
				0,
				stemOf(key),
				sourceKey,
				stamp,
				key,
				beforeWrite,
				onWritten,
				cancel);
		}

		if (wants(EnvironmentPart::kLighting))
		{
			const std::string& key = keys[EnvironmentPart::kLighting];
			static_cast<void>(produceLighting(
				store,
				input,
				parameters,
				0,
				stemOf(key),
				sourceKey,
				stamp,
				key,
				beforeWrite,
				onWritten,
				cancel));
		}
	}
}
