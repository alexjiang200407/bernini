#include <assetlib/migrate.h>

#include <assetlib/AssetStore.h>
#include <assetlib/RegenMesh.h>
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
#include "ref_paths.h"

#include <core/err/util.h>
#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		/**
		 * The bytes the project's current state says `key` should hold, or nullopt for a type
		 * this does not migrate. Geometry goes through the regeneration seam, so a stale group
		 * re-cooks from its copied source and a binding-only document edit reaches disk without
		 * one (a binding naming a vanished submesh is this file's failure); everything else is
		 * read and re-saved at the current form.
		 */
		std::optional<std::vector<std::byte>>
		resave(
			const AssetStore&          store,
			AssetType                  type,
			std::string_view           key,
			std::span<const std::byte> bytes)
		{
			switch (type)
			{
			case AssetType::kMesh:
			{
				RegenMesh current = store.LoadRegenMesh(key);
				core::throw_runtime_error_if(
					!current.unboundBindings.empty(),
					"its import document binds submesh '{}', which the mesh does not have; "
					"rebind or re-export",
					current.unboundBindings.front());
				return serialize(current.mesh);
			}
			case AssetType::kSkeleton:
				return serializeSkeleton(store.LoadRegenSkeleton(key));
			case AssetType::kAnimation:
				return serializeAnimations(store.LoadRegenAnimations(key));
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
			case AssetType::kImportDocument:
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

		// Meshes first, then rigs, then clips: a regenerated `.banim` re-measures its posed
		// boxes against the meshes on disk, so the meshes must be current before it looks.
		const auto rank = [](const std::filesystem::path& path) {
			const auto type = assetTypeFromExtension(path);
			if (type == AssetType::kMesh)
				return 0;
			if (type == AssetType::kSkeleton)
				return 1;
			if (type == AssetType::kAnimation)
				return 2;
			return 3;
		};
		std::ranges::sort(paths, [&rank](const auto& a, const auto& b) {
			return std::pair(rank(a), std::ref(a)) < std::pair(rank(b), std::ref(b));
		});

		const AssetStore store(dataRoot);

		MigrateReport report;
		for (const std::filesystem::path& path : paths)
		{
			const auto type = assetTypeFromExtension(path);
			if (!type)
				continue;

			MigratedFile file{ path, MigratedFile::Outcome::kUnchanged, {} };
			try
			{
				const std::string key =
					normalizeRef(path.lexically_relative(dataRoot).generic_string());

				const auto bytes   = core::file::read_file_bytes(path.string());
				const auto current = resave(store, *type, key, bytes);
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
