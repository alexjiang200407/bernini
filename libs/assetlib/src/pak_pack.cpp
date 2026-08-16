#include <assetlib/pak_pack.h>

#include <assetlib/asset_refs.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/pak_io.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BVat.h>

#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>

#include "ref_paths.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_AuthoringDir = "textures_src";

		bool
		isAuthoringSource(const std::filesystem::path& relative)
		{
			for (const std::filesystem::path& part : relative)
			{
				if (part == c_AuthoringDir)
					return true;
			}
			return false;
		}

		std::string
		relativeKey(const std::filesystem::path& file, const std::filesystem::path& dataRoot)
		{
			return normalizeRef(file.lexically_relative(dataRoot).generic_string());
		}

		std::vector<std::filesystem::path>
		filesUnder(const std::filesystem::path& dataRoot)
		{
			std::vector<std::filesystem::path> out;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
			{
				if (entry.is_regular_file())
					out.push_back(entry.path());
			}
			return out;
		}

		// A packed .bvat's inputs may ship beside it with nowhere to write a re-bake, so the archive
		// carries one that is correct at pack time rather than one the runtime has to judge.
		uint32_t
		rebakeStaleVats(
			const std::filesystem::path&              dataRoot,
			const std::vector<std::filesystem::path>& files)
		{
			const core::file::LooseFileSystem loose(dataRoot);

			uint32_t rebaked = 0;
			for (const std::filesystem::path& file : files)
			{
				if (assetTypeFromExtension(file) != AssetType::kVat)
					continue;

				// Tables alone: the pixels are the bulk of the file, and both the staleness verdict
				// and the paths to re-bake from are in the chunks this reads.
				const BVat tables = loadVatTables(file);
				if (!vatIsStale(tables, loose))
					continue;

				saveVat(
					bakeVat(AssetStore(dataRoot), VatBakeDesc{ tables.mesh, tables.animations }),
					file);
				++rebaked;
			}
			return rebaked;
		}
	}

	PackReport
	packProject(const AssetStore& store, const PackDesc& desc)
	{
		// The walk and the rebake address the writable layer: packing reads what is on disk under
		// the data root, not what a wider mount would also answer for.
		const std::filesystem::path& dataRoot = store.GetDataRoot();

		if (!std::filesystem::is_directory(dataRoot))
			core::throw_runtime_error(
				"assetlib::packProject: '{}' is not a directory",
				dataRoot.string());

		const std::vector<std::filesystem::path> files = filesUnder(dataRoot);

		PackReport report;
		report.vatsRebaked = rebakeStaleVats(dataRoot, files);

		const core::file::LooseFileSystem loose(dataRoot);

		PakWriter writer(desc.target);
		for (const std::filesystem::path& file : files)
		{
			const std::filesystem::path relative = file.lexically_relative(dataRoot);
			if (isAuthoringSource(relative))
				continue;

			const std::optional<AssetType> type = assetTypeFromExtension(file);
			if (!type.has_value())
			{
				++report.skippedByExtension[file.extension().generic_string()];
				continue;
			}

			const std::string                          key   = relativeKey(file, dataRoot);
			const std::vector<std::byte>               bytes = loose.Read(key);
			const std::optional<core::file::FileStamp> stamp = loose.Stat(key);
			if (!stamp.has_value())
				core::throw_runtime_error("assetlib::packProject: cannot stat '{}'", key);

			// Asked while the bytes are already in hand, which is the only moment packing reads a
			// material at all.
			if (type == AssetType::kMaterial && drawsLoose(deserializeMaterial(bytes), loose))
				report.materialsDrawingLoose.push_back(key);

			writer.Add(key, bytes, *stamp);

			++report.entries;
			report.payloadBytes += bytes.size();
		}

		std::ranges::sort(report.materialsDrawingLoose);

		writer.Finish();
		return report;
	}
}
