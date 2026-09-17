#pragma once

#include <assetlib/cancel.h>
#include <assetlib/env_import_parameters.h>
#include <assetlib_structs/ImageData.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace assetlib
{
	class AssetStore;

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

	/** One file a part may write: its mount key, and whether this run writes it. */
	struct EnvironmentTarget
	{
		std::string key;
		bool        write = true;
	};

	struct SkyTargets
	{
		EnvironmentTarget source;     // the float chain
		EnvironmentTarget container;  // the `.bsky` baked from it
	};

	struct LightingTargets
	{
		EnvironmentTarget prefilter;   // float
		EnvironmentTarget irradiance;  // float
		EnvironmentTarget container;   // the `.benvl` baked from the two
	};

	/** Told one environment file's mount key; when, is each parameter's name to say. */
	using EnvironmentFileSink = std::function<void(const std::string& key)>;

	/**
	 * Writes the sky's targets that are marked for writing. The one writer `ImportEnvironment` and
	 * `Reimport` share, so a file either produces is byte for byte the file the other would.
	 *
	 * @pre `targets.source.key` names the float chain whether or not this run writes it: the
	 *      `.bsky` bakes from that file on disk.
	 */
	void
	produceSky(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const SkyTargets&                  targets,
		const EnvironmentFileSink&         beforeWrite,
		const CancelToken&                 cancel);

	/**
	 * The lighting's counterpart of produceSky.
	 *
	 * @return The exposure the bake derived, or nullopt when this run wrote no `.benvl`.
	 */
	std::optional<float>
	produceLighting(
		const AssetStore&                  store,
		EnvironmentInput&                  input,
		const EnvironmentImportParameters& parameters,
		uint32_t                           threads,
		std::string_view                   name,
		const LightingTargets&             targets,
		const EnvironmentFileSink&         beforeWrite,
		const CancelToken&                 cancel);
}
