#include <assetlib/texture_prune.h>

#include <assetlib/bmaterial_io.h>
#include <assetlib/material_bake.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_MaterialExtension = ".bmaterial";

		struct LiveSet
		{
			std::unordered_set<std::string> maps;
			size_t                          materials = 0;
		};

		/**
		 * The mark phase: the name of every baked map that some material below `dataRoot` still points
		 * at.
		 *
		 * Keyed by file name rather than by the path the material stores. A baked name is a content hash
		 * of the map, so the name alone identifies it -- and matching on the name also keeps a map alive
		 * that a material reached through some other `textureDir`. Both are errors in the direction of
		 * keeping a file, which is the only direction a prune may err in.
		 */
		LiveSet
		markLiveMaps(const std::filesystem::path& dataRoot)
		{
			auto live = LiveSet();

			for (const auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
			{
				if (!entry.is_regular_file() || entry.path().extension() != c_MaterialExtension)
					continue;

				auto material = BMaterial();
				try
				{
					material = loadMaterial(entry.path());
				}
				catch (const std::exception& e)
				{
					// Fatal, and deliberately so: a material we cannot read is a material whose maps we
					// cannot mark, and they would then be swept as garbage.
					throw std::runtime_error(
						"assetlib::findUnusedBakedTextures: cannot read the material '" +
						entry.path().string() +
						"', so the baked maps it references cannot be known: " + e.what());
				}

				++live.materials;

				// The triplet whatever the material's `mode` says. A kLoose material still carries the
				// maps its last bake wrote, and they are a valid bake to switch back to -- deleting them
				// because the renderer happens to be drawing from the routes today would destroy it.
				for (const std::string* map :
				     { &material.baseColorTexture, &material.normalTexture, &material.ormTexture })
				{
					if (!map->empty())
						live.maps.insert(std::filesystem::path(*map).filename().string());
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

		const LiveSet live    = markLiveMaps(desc.dataRoot);
		scan.materialsScanned = live.materials;
		scan.liveMaps         = live.maps.size();

		// Nothing has ever been baked here, so nothing can have been orphaned.
		const std::filesystem::path textureDir = desc.dataRoot / desc.textureDir;
		if (!std::filesystem::is_directory(textureDir))
			return scan;

		for (const auto& entry : std::filesystem::directory_iterator(textureDir))
		{
			if (!entry.is_regular_file())
				continue;

			const std::string name = entry.path().filename().string();

			// A file the bake could not have written is not ours to delete, whoever put it here:
			// skybox.ktx2 and brdf_lut.ktx2 share this directory and are referenced by config, not by
			// any material.
			if (!isBakedMapName(name))
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

			// `removed` is false, with no error, for a file that had already gone. That is the outcome
			// the caller asked for, so it counts as deleted -- but its bytes were reclaimed by whoever
			// got there first, not by us.
			++result.deleted;
			if (removed)
				result.bytes += texture.bytes;
		}

		return result;
	}
}
