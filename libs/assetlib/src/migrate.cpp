#include <assetlib/migrate.h>

#include <assetlib/asset_refs.h>
#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "fs_util.h"

#include <core/err/util.h>
#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		/** The bytes the current serializer writes for what `bytes` holds, or nullopt for a type this does not migrate. */
		std::optional<std::vector<std::byte>>
		resave(AssetType type, std::span<const std::byte> bytes)
		{
			switch (type)
			{
			case AssetType::kMesh:
				return serialize(deserialize(bytes));
			case AssetType::kSkeleton:
				return serializeSkeleton(deserializeSkeleton(bytes));
			case AssetType::kAnimation:
				return serializeAnimations(deserializeAnimations(bytes));
			case AssetType::kMaterial:
				return serializeMaterial(deserializeMaterial(bytes));
			case AssetType::kSky:
				return serializeSky(deserializeSky(bytes));
			case AssetType::kEnvLighting:
				return serializeEnvLighting(deserializeEnvLighting(bytes));
			case AssetType::kEnvironment:
				return serializeEnv(deserializeEnv(bytes));
			case AssetType::kTexture:
			case AssetType::kVat:
				return std::nullopt;
			}
			return std::nullopt;
		}
	}

	size_t
	MigrateReport::Count(MigratedFile::Outcome outcome) const noexcept
	{
		return static_cast<size_t>(std::ranges::count(files, outcome, &MigratedFile::outcome));
	}

	MigrateReport
	migrateProject(const std::filesystem::path& dataRoot, bool dryRun)
	{
		core::throw_runtime_error_if(
			!std::filesystem::is_directory(dataRoot),
			"migrate: {} is not a directory",
			dataRoot.string());

		std::vector<std::filesystem::path> paths;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(dataRoot))
			if (entry.is_regular_file())
				paths.push_back(entry.path());
		std::ranges::sort(paths);

		MigrateReport report;
		for (const std::filesystem::path& path : paths)
		{
			const auto type = assetTypeFromExtension(path);
			if (!type)
				continue;

			MigratedFile file{ path, MigratedFile::Outcome::kUnchanged, {} };
			try
			{
				const auto bytes   = core::file::read_file_bytes(path.string());
				const auto current = resave(*type, bytes);
				if (!current)
					continue;
				if (*current != bytes)
				{
					if (!dryRun)
						writeFileBytes(path, *current, "migrate");
					file.outcome = MigratedFile::Outcome::kRewritten;
				}
			}
			catch (const std::exception& error)
			{
				file.outcome = MigratedFile::Outcome::kFailed;
				file.message = error.what();
			}
			report.files.push_back(std::move(file));
		}
		return report;
	}
}
