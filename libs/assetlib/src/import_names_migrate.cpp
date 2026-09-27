#include "ref_paths.h"
#include <assetlib/AssetStore.h>
#include <assetlib/ImportIdentity.h>
#include <assetlib/asset_refs.h>
#include <assetlib/avatar.h>
#include <assetlib/codecs.h>
#include <assetlib/import_document.h>
#include <assetlib/migrate.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/BMaterial.h>
#include <core/err/util.h>
#include <core/str/str.h>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace assetlib
{
	void
	AssetStore::MigrateImportNames(bool dryRun, MigrateReport& report) const
	{
		ZoneScopedN("assetlib migrate import names");
		auto documents      = std::unordered_set<std::string>();
		auto textureOwners  = core::str::unordered_str_map<std::string>();
		auto identityOwners = std::unordered_map<uint64_t, std::string>();
		auto blocked        = std::unordered_set<std::string>();
		for (const auto category : { c_MeshSourcesDirectoryName, c_EnvSourcesDirectoryName })
			for (const auto& key : GetFiles().Enumerate(category))
			{
				if (extensionOf(key) != c_ImportDocumentExtension)
					continue;
				try
				{
					auto document = Load<ImportDocument>(key);
					if (document.identity.id != 0)
					{
						const auto [owner, inserted] =
							identityOwners.emplace(document.identity.id, key);
						if (!inserted)
						{
							blocked.insert(owner->second);
							blocked.insert(key);
						}
					}
					if (!document.textureDir.empty())
					{
						const auto [owner, inserted] =
							textureOwners.emplace(document.textureDir, key);
						if (!inserted)
						{
							blocked.insert(owner->second);
							blocked.insert(key);
						}
					}
					documents.insert(key);
				}
				catch (const std::exception& error)
				{
					report.files.push_back(
						{ GetDataRoot() / key, MigratedFile::Outcome::kFailed, error.what() });
				}
			}
		for (const auto& key : blocked)
		{
			documents.erase(key);
			report.files.push_back(
				{ GetDataRoot() / key,
			      MigratedFile::Outcome::kFailed,
			      "import identity or extracted-texture directory is shared by multiple sources" });
		}
		ZoneTextF("%zu sources", documents.size());
		if (documents.empty())
			return;

		const auto graph            = AssetRefGraph::Scan(*this);
		auto       textureReferrers = core::str::unordered_str_map<std::vector<AssetRef>>();
		for (const auto& edge : graph.Edges())
		{
			std::string_view directory = edge.target;
			for (auto slash = directory.find_last_of('/'); slash != std::string_view::npos;
			     slash      = directory.find_last_of('/'))
			{
				directory = directory.substr(0, slash);
				if (textureOwners.contains(directory))
					textureReferrers[std::string(directory)].push_back(edge);
			}
		}
		auto movedPaths = core::str::unordered_str_map<std::string>();
		auto materials  = std::unordered_set<std::string>();
		for (const auto& key : documents)
		{
			try
			{
				auto       document = Load<ImportDocument>(key);
				const bool mint     = document.identity.id == 0;
				if (mint)
					document.identity = makeImportIdentity(importedSourceKeyFor(key, document));
				auto plan      = RenamePlan();
				plan.subject   = { key, key };
				plan.assetType = AssetType::kImportDocument;
				plan.registry  = GetKindRegistry();
				for (const auto& output : document.outputs)
				{
					const auto type = assetTypeFromExtension(output);
					if (!type || (document.environment ? (*type != AssetType::kSky &&
					                                      *type != AssetType::kEnvLighting) :
					                                     !isGeometryContainer(*type)))
						core::throw_runtime_error("'{}' is not an output of this import", output);
					const auto target =
						*type == AssetType::kGrassFields ?
							swapExtension(
								importOutputKey(document.identity, AssetType::kMesh),
								".bgrassfields") :
							importOutputKey(document.identity, *type);
					if (output == target)
						continue;
					plan.outputs.push_back({ output, target });
					for (const auto& ref : graph.ReferrersOf(output))
						if (isStoredRef(ref.kind))
							plan.referrers.push_back(ref);
					if (*type == AssetType::kSkeleton && Exists(avatarKeyFor(output)))
						plan.avatars.push_back({ avatarKeyFor(output), avatarKeyFor(target) });
				}
				if (!document.textureDir.empty())
				{
					const auto target = importTextureDirectory(document.identity);
					if (document.textureDir != target)
					{
						plan.outputs.push_back({ document.textureDir, target });
						for (const auto& ref : textureReferrers[document.textureDir])
							if (isStoredRef(ref.kind))
								plan.referrers.push_back(ref);
					}
				}
				if (!mint && plan.outputs.empty())
					continue;
				if (!dryRun)
				{
					// Persist the identity before moving anything so an interrupted migration resumes it.
					Save(document, key);
					plan.referrers.push_back({});
					plan.referrers.back().referrer = key;
					for (auto& ref : plan.referrers)
						if (const auto moved = movedPaths.find(ref.referrer);
						    moved != movedPaths.end())
							ref.referrer = moved->second;
					if (!plan.outputs.empty())
					{
						const auto result = RenameAsset(plan);
						if (result.status != RenameStatus::kRenamed)
							core::throw_runtime_error("{}", result.error);
						for (const auto& move : plan.outputs) movedPaths[move.from] = move.to;
						for (const auto& move : plan.avatars) movedPaths[move.from] = move.to;
						for (const auto& ref : plan.referrers)
							if (assetTypeFromExtension(ref.referrer) == AssetType::kMaterial)
								materials.insert(ref.referrer);
					}
				}
				report.files.push_back(
					{ GetDataRoot() / key,
				      MigratedFile::Outcome::kRewritten,
				      "generated import destinations" });
			}
			catch (const std::exception& error)
			{
				report.files.push_back(
					{ GetDataRoot() / key, MigratedFile::Outcome::kFailed, error.what() });
			}
		}
		for (const auto& key : materials)
		{
			try
			{
				auto material = Load<BMaterial>(key);
				BakeMaterial(material);
				Save(material, key);
			}
			catch (const std::exception& error)
			{
				report.files.push_back(
					{ GetDataRoot() / key, MigratedFile::Outcome::kFailed, error.what() });
			}
		}
	}
}
