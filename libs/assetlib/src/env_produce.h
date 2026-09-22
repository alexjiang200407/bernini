#pragma once

#include <assetlib/cancel.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib_structs/ImageData.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace assetlib
{
	class AssetStore;
	struct ImportDocument;
	struct SourceStamp;

	/**
	 * One environment source, decoded once and projected at each face size a part asks for. A cube
	 * source is already a cube and serves every size as it stands.
	 */
	class EnvironmentInput
	{
	public:
		/**
		 * A baked RGB9E5 cube is unpacked to float with a warning: re-convolving one quantizes twice.
		 *
		 * @throws what loadRadianceHdr and loadKTX2 throw.
		 */
		explicit EnvironmentInput(const std::filesystem::path& source);

		// Move-only, following ImageData: a decoded source and its cubes are megabytes each.
		EnvironmentInput(EnvironmentInput&&)      = default;
		EnvironmentInput(const EnvironmentInput&) = delete;
		EnvironmentInput&
		operator=(EnvironmentInput&&) = default;
		EnvironmentInput&
		operator=(const EnvironmentInput&) = delete;

		[[nodiscard]] const ImageData&
		CubeAt(uint32_t faceSize);

	private:
		ImageData                               m_Input;
		bool                                    m_Equirect = false;
		std::unordered_map<uint32_t, ImageData> m_Cubes;
	};

	/**
	 * The sky's defocus chain, projected and convolved in memory from `input` at `parameters` --
	 * what a `.bsky` bakes. Its mip count is clamped to what the face size can carry.
	 */
	[[nodiscard]] ImageData
	skyChainOf(
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads);

	/** The two convolutions a `.benvl` bakes, of one radiance. */
	struct LightingMaps
	{
		ImageData prefilter;
		ImageData irradiance;
	};

	/** The lighting's counterpart of skyChainOf: minutes of convolution, not seconds. */
	[[nodiscard]] LightingMaps
	lightingMapsOf(
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads);

	/** Told one environment file's mount key; when, is each parameter's name to say. */
	using EnvironmentFileSink = std::function<void(const std::string& key)>;

	/**
	 * Cooks the sky from `input` and writes its `.bsky` at `containerKey`, routing `sourceKey` as it
	 * stood at `stamp`, telling `beforeWrite` and then `afterWrite` the key around the save; either
	 * may be empty. The one writer `ImportEnvironment` and `Reimport` share, so a file either
	 * produces is byte for byte the file the other would.
	 */
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
		const CancelToken&                 cancel);

	/**
	 * The lighting's counterpart of produceSky.
	 *
	 * @return The exposure the bake derived.
	 */
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
		const CancelToken&                 cancel);

	/**
	 * Writes the files `wanted` names out of one environment source's import document, each part
	 * re-run for only those. What `Reimport` produces an absent file with and what a refresh re-cooks
	 * a stale part with.
	 *
	 * @param onWritten Told each file as soon as it is on disk, so one written before a later step
	 *        throws is still told.
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
		const CancelToken&              cancel);
}
