#include <assetlib/texture_prune.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

#include "ref_paths.h"

#include <assetlib/container_format.h>
#include <assetlib/env_bake.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
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
		 * The mark phase: the name of every baked map that some material, sky or lighting asset in
		 * `files` still points at. An unreadable asset is fatal, and deliberately so -- its maps
		 * cannot be marked, and they would then be swept as garbage.
		 *
		 * Reads the whole mount and not only the writable layer: a packed material holds a loose map
		 * alive exactly as a loose one does, and marking only what can be deleted would sweep the maps
		 * that the archive is the sole referrer of.
		 */
		LiveSet
		markLiveMaps(const core::file::IFileSystem& files)
		{
			auto live = LiveSet();

			for (const std::string& key : files.Enumerate())
			{
				const auto unreadable = [&key](const char* what, const std::exception& e) {
					return std::runtime_error(
						"assetlib::findUnusedBakedTextures: cannot read the " + std::string(what) +
						" '" + key +
						"', so the baked maps it references cannot be known: " + e.what());
				};

				const std::string extension = extensionOf(key);
				if (extension == c_MaterialExtension)
				{
					auto material = BMaterial();
					try
					{
						material = load<BMaterial>(files, key);
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
							"assetlib::findUnusedBakedTextures: the material '" + key +
							"' names an unknown shading model, so its baked maps cannot be known");
					}
				}
				else if (extension == c_SkyExtension)
				{
					try
					{
						markMap(live, load<BSky>(files, key).sky.baked);
					}
					catch (const std::exception& e)
					{
						throw unreadable("sky", e);
					}
					++live.environments;
				}
				else if (extension == c_EnvLightingExtension)
				{
					try
					{
						const BEnvLighting lighting = load<BEnvLighting>(files, key);
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
	findUnusedBakedTextures(const AssetStore& store, const TexturePruneDesc& desc)
	{
		// Checked even though a source built from a path already was: this one may have been built
		// over a mount, whose constructor cannot check a writable layer that is allowed not to exist
		// yet. The sweep unlinks files, and a data root that is not there would report a clean
		// project instead of the caller error it is.
		if (!std::filesystem::is_directory(store.GetDataRoot()))
			core::throw_runtime_error(
				"assetlib::findUnusedBakedTextures: the data root '{}' is not a directory",
				store.GetDataRoot().string());

		const core::file::IFileSystem& files = store.GetFiles();

		auto scan = TexturePruneScan();

		const LiveSet live       = markLiveMaps(files);
		scan.materialsScanned    = live.materials;
		scan.environmentsScanned = live.environments;
		scan.liveMaps            = live.maps.size();

		// The sweep stays on the writable layer while the mark read the whole mount: a packed entry
		// cannot be unlinked, so proposing one would be proposing a deletion nobody can carry out.
		const std::filesystem::path textureDir = store.GetDataRoot() / desc.textureDir;
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
	deleteUnusedBakedTextures(const TexturePruneScan& scan, const AssetStore& store)
	{
		auto result = TexturePruneResult();

		for (const UnusedTexture& texture : scan.unused)
		{
			std::error_code ec;
			const bool removed = std::filesystem::remove(store.GetDataRoot() / texture.path, ec);

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
