#include <assetlib/texture_prune.h>

#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_MaterialExtension = ".bmaterial";
		constexpr std::string_view c_SkyExtension      = ".bsky";
		constexpr std::string_view c_LightingExtension = ".benvl";

		struct LiveSet
		{
			std::unordered_set<std::string> maps;
			size_t                          materials    = 0;
			size_t                          environments = 0;
		};

		void
		markMap(LiveSet& live, const std::string& map)
		{
			if (!map.empty())
				live.maps.insert(std::filesystem::path(map).filename().string());
		}

		/**
		 * The mark phase: the name of every baked map that some material, sky or lighting asset below
		 * `dataRoot` still points at. An unreadable asset is fatal, and deliberately so -- its maps
		 * cannot be marked, and they would then be swept as garbage.
		 */
		LiveSet
		markLiveMaps(const std::filesystem::path& dataRoot)
		{
			auto live = LiveSet();

			for (const auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
			{
				if (!entry.is_regular_file())
					continue;

				const auto unreadable = [&entry](const char* what, const std::exception& e) {
					return std::runtime_error(
						"assetlib::findUnusedBakedTextures: cannot read the " + std::string(what) +
						" '" + entry.path().string() +
						"', so the baked maps it references cannot be known: " + e.what());
				};

				const auto extension = entry.path().extension();
				if (extension == c_MaterialExtension)
				{
					auto material = BMaterial();
					try
					{
						material = loadMaterial(entry.path());
					}
					catch (const std::exception& e)
					{
						throw unreadable("material", e);
					}

					++live.materials;

					switch (material.shadingModel)
					{
					case ShadingModel::kPbr:
						markMap(live, material.pbr.baseColorTexture);
						markMap(live, material.pbr.normalTexture);
						markMap(live, material.pbr.ormTexture);
						break;

					case ShadingModel::kCount:
						throw std::runtime_error(
							"assetlib::findUnusedBakedTextures: the material '" +
							entry.path().string() +
							"' names an unknown shading model, so its baked maps cannot be known");
					}
				}
				else if (extension == c_SkyExtension)
				{
					try
					{
						markMap(live, loadSky(entry.path()).sky.baked);
					}
					catch (const std::exception& e)
					{
						throw unreadable("sky", e);
					}
					++live.environments;
				}
				else if (extension == c_LightingExtension)
				{
					try
					{
						const BEnvLighting lighting = loadEnvLighting(entry.path());
						markMap(live, lighting.prefilter.baked);
						markMap(live, lighting.irradiance.baked);
					}
					catch (const std::exception& e)
					{
						throw unreadable("env lighting", e);
					}
					++live.environments;
				}
			}

			return live;
		}
	}

	TexturePruneScan
	findUnusedBakedTextures(const TexturePruneDesc& desc)
	{
		if (!std::filesystem::is_directory(desc.dataRoot))
			throw std::runtime_error(
				"assetlib::findUnusedBakedTextures: the data root '" + desc.dataRoot.string() +
				"' is not a directory");

		auto scan = TexturePruneScan();

		const LiveSet live       = markLiveMaps(desc.dataRoot);
		scan.materialsScanned    = live.materials;
		scan.environmentsScanned = live.environments;
		scan.liveMaps            = live.maps.size();

		// Nothing has ever been baked here, so nothing can have been orphaned.
		const std::filesystem::path textureDir = desc.dataRoot / desc.textureDir;
		if (!std::filesystem::is_directory(textureDir))
			return scan;

		for (const auto& entry : std::filesystem::directory_iterator(textureDir))
		{
			if (!entry.is_regular_file())
				continue;

			const std::string name = entry.path().filename().string();

			if (!isBakedMapName(name) && !isBakedEnvMapName(name))
				continue;

			++scan.candidates;

			if (live.maps.contains(name))
				continue;

			const auto bytes = static_cast<uint64_t>(entry.file_size());
			scan.unused.push_back(
				UnusedTexture{ (desc.textureDir / name).generic_string(), bytes });
			scan.bytes += bytes;
		}

		// A stable order, so a dry run lists what the run that follows it deletes.
		std::ranges::sort(scan.unused, {}, &UnusedTexture::path);

		return scan;
	}

	TexturePruneResult
	deleteUnusedBakedTextures(const TexturePruneScan& scan, const TexturePruneDesc& desc)
	{
		auto result = TexturePruneResult();

		for (const UnusedTexture& texture : scan.unused)
		{
			std::error_code ec;
			const bool      removed = std::filesystem::remove(desc.dataRoot / texture.path, ec);

			// One locked map must not abandon the rest of the sweep.
			if (ec)
			{
				result.failed.push_back(texture.path);
				continue;
			}

			++result.deleted;
			if (removed)
				result.bytes += texture.bytes;
		}

		return result;
	}
}
