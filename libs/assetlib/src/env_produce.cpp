#include "env_produce.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/cancel.h>
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
#include <initializer_list>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "env_parts.h"
#include "fs_util.h"
#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_HdrExtension = ".hdr";

		// The sky's defocus chain convolves every level below the first with this many samples.
		constexpr uint32_t c_SkyChainSamples = 256;

		void
		writeFloatCube(const AssetStore& store, const std::string& key, const ImageData& image)
		{
			const std::filesystem::path path = store.ResolveWritePath(key);
			createDirectories(path.parent_path());
			writeKTX2(image, path, false, Ktx2Compression::kNone);
		}
	}

	EnvironmentInput::EnvironmentInput(const std::filesystem::path& source) :
		m_Equirect(extensionOf(source.generic_string()) == c_HdrExtension)
	{
		m_Input = m_Equirect ? loadRadianceHdr(source) : loadKTX2(source);

		// A shipped map is RGB9E5, and that is the only form left when a route's float source has
		// gone. Re-convolving one is a recovery path, not the one to reach for.
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

	void
	produceSky(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const SkyTargets&                  targets,
		const EnvironmentFileSink&         beforeWrite,
		const CancelToken&                 cancel)
	{
		if (targets.source.write)
		{
			throwIfCancelled(cancel);

			// A chain, never a single blurred mip: the backdrop's defocus is presentation, so it
			// belongs on the `.benv` document where a viewer can change it.
			//
			// Clamped rather than refused: a sky too small for the requested chain is a small sky,
			// not a bad request.
			const auto maxMips =
				static_cast<uint32_t>(std::bit_width(std::max(parameters.skyFaceSize, 1u)));
			const ImageData chain = skyChain(
				input.CubeAt(parameters.skyFaceSize),
				parameters.skyFaceSize,
				std::clamp(parameters.skyMips, 1u, maxMips),
				c_SkyChainSamples,
				threads);

			beforeWrite(targets.source.key);
			writeFloatCube(store, targets.source.key, chain);
		}

		if (targets.container.write)
		{
			auto bsky       = BSky();
			bsky.name       = std::string(name);
			bsky.sky.source = targets.source.key;

			throwIfCancelled(cancel);
			store.BakeSky(bsky, cancel);

			beforeWrite(targets.container.key);
			store.Save(bsky, targets.container.key);
		}
	}

	std::optional<float>
	produceLighting(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const LightingTargets&             targets,
		const EnvironmentFileSink&         beforeWrite,
		const CancelToken&                 cancel)
	{
		if (targets.irradiance.write)
		{
			throwIfCancelled(cancel);
			const ImageData irradiance = irradianceSh(
				input.CubeAt(lightingProjectionSize(parameters)),
				parameters.irradianceFaceSize);

			beforeWrite(targets.irradiance.key);
			writeFloatCube(store, targets.irradiance.key, irradiance);
		}

		if (targets.prefilter.write)
		{
			auto prefilterDesc      = PrefilterDesc();
			prefilterDesc.faceSize  = parameters.prefilterFaceSize;
			prefilterDesc.mipLevels = parameters.prefilterMips;
			prefilterDesc.samples   = parameters.prefilterSamples;
			prefilterDesc.threads   = threads;

			throwIfCancelled(cancel);
			const ImageData prefilter =
				prefilterRadiance(input.CubeAt(lightingProjectionSize(parameters)), prefilterDesc);

			beforeWrite(targets.prefilter.key);
			writeFloatCube(store, targets.prefilter.key, prefilter);
		}

		if (!targets.container.write)
			return std::nullopt;

		auto lighting              = BEnvLighting();
		lighting.name              = std::string(name);
		lighting.prefilter.source  = targets.prefilter.key;
		lighting.irradiance.source = targets.irradiance.key;

		throwIfCancelled(cancel);
		store.BakeEnvLighting(lighting, cancel);

		beforeWrite(targets.container.key);
		store.Save(lighting, targets.container.key);
		return lighting.exposure;
	}

	/**
	 * Produces the files `wanted` names out of one environment source's document: each part
	 * re-run for only the files it is missing, so a lost `.bsky` beside its float chain is a
	 * bake and not a convolution.
	 *
	 * @param onWritten Told each file once it is on disk.
	 * @throws std::runtime_error if the document names no parameters, or claims a file no
	 *         environment import writes.
	 */
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
		core::throw_runtime_error_if(
			!document.environment,
			"'{}': its import document records no environment parameters",
			sourceKey);

		auto keys = std::unordered_map<EnvironmentOutput, std::string>();
		for (const std::string& output : document.outputs)
		{
			const std::optional<EnvironmentOutput> role = environmentOutputOf(output);
			core::throw_runtime_error_if(
				!role,
				"'{}': its import document claims '{}', which no environment import writes",
				sourceKey,
				output);
			keys[*role] = output;
		}

		const auto target = [&](EnvironmentOutput role) -> EnvironmentTarget {
			const std::string& key = keys[role];
			return { key, std::ranges::find(wanted, key) != wanted.end() };
		};

		// A container bakes from the float cubes it routes, so it cannot be produced without
		// knowing their names.
		const auto requireFeeds = [&](EnvironmentOutput                        container,
		                              std::initializer_list<EnvironmentOutput> feeds) {
			if (keys[container].empty())
				return;
			for (const EnvironmentOutput feed : feeds)
				core::throw_runtime_error_if(
					keys[feed].empty(),
					"'{}': its import document claims '{}' but not the float cube it bakes "
					"from",
					sourceKey,
					keys[container]);
		};
		requireFeeds(EnvironmentOutput::kSky, { EnvironmentOutput::kSkySource });
		requireFeeds(
			EnvironmentOutput::kLighting,
			{ EnvironmentOutput::kPrefilterSource, EnvironmentOutput::kIrradianceSource });

		// The previous file reports written only once the next one starts, so a write that
		// throws is never counted.
		auto       last  = std::optional<std::string>();
		const auto track = [&](const std::string& key) {
			if (last)
				onWritten(*last);
			last = key;
			beforeWrite(key);
		};

		auto input = EnvironmentInput(store.ResolveWritePath(sourceKey));
		const EnvironmentImportParameters& parameters = *document.environment;

		const SkyTargets sky = { .source    = target(EnvironmentOutput::kSkySource),
			                     .container = target(EnvironmentOutput::kSky) };
		if (sky.source.write || sky.container.write)
			produceSky(store, input, parameters, 0, stemOf(sky.container.key), sky, track, cancel);

		const LightingTargets lighting = { .prefilter = target(EnvironmentOutput::kPrefilterSource),
			                               .irradiance =
			                                   target(EnvironmentOutput::kIrradianceSource),
			                               .container = target(EnvironmentOutput::kLighting) };
		if (lighting.prefilter.write || lighting.irradiance.write || lighting.container.write)
			static_cast<void>(produceLighting(
				store,
				input,
				parameters,
				0,
				stemOf(lighting.container.key),
				lighting,
				track,
				cancel));

		if (last)
			onWritten(*last);
	}
}
