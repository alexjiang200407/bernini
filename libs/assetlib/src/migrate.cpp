#include <assetlib/codecs.h>
#include <assetlib/migrate.h>

#include <assetlib/AssetStore.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <assetlib/project_layout.h>
#include <assetlib/reimport.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "fs_util.h"
#include "material_texture_refs.h"
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
				return AssetCodec<BMesh>::Serialize(current.mesh);
			}
			case AssetType::kSkeleton:
				return AssetCodec<Skeleton>::Serialize(store.LoadRegenSkeleton(key));
			case AssetType::kAnimation:
				return AssetCodec<AnimationSet>::Serialize(store.LoadRegenAnimations(key));
			case AssetType::kMaterial:
				return AssetCodec<BMaterial>::Serialize(AssetCodec<BMaterial>::Deserialize(bytes));
			case AssetType::kSky:
				return AssetCodec<BSky>::Serialize(AssetCodec<BSky>::Deserialize(bytes));
			case AssetType::kEnvLighting:
				return AssetCodec<BEnvLighting>::Serialize(
					AssetCodec<BEnvLighting>::Deserialize(bytes));
			case AssetType::kEnvironment:
				return AssetCodec<BEnv>::Serialize(AssetCodec<BEnv>::Deserialize(bytes));
			case AssetType::kTexture:
			case AssetType::kVat:
			case AssetType::kImportDocument:
			case AssetType::kCount:
				return std::nullopt;
			}
			return std::nullopt;
		}

		/** What one source produced, and the rig its containers name. */
		struct SourceFacts
		{
			std::string              skeleton;
			std::vector<std::string> outputs;
		};

		/**
		 * The two document fields read back out of the derived files, for a project whose documents
		 * predate them: a container's header names the source it came from, and a `.bmesh` or
		 * `.banim` already stores the rig its indices address.
		 */
		std::unordered_map<std::string, SourceFacts>
		factsFromDerived(const AssetStore& store, std::span<const std::filesystem::path> paths)
		{
			auto facts = std::unordered_map<std::string, SourceFacts>();

			for (const std::filesystem::path& path : paths)
			{
				const auto type = assetTypeFromExtension(path);
				if (!type.has_value() || !isGeometryContainer(*type))
					continue;

				try
				{
					const std::string key =
						normalizeRef(path.lexically_relative(store.GetDataRoot()).generic_string());

					const std::string source = store.GeometryGroupSource(key).key;
					if (source.empty())
						continue;

					SourceFacts& entry = facts[source];
					entry.outputs.push_back(key);

					if (type == AssetType::kMesh)
						entry.skeleton = loadMeshRefs(path).skeleton;
					else if (type == AssetType::kAnimation)
						entry.skeleton = loadAnimationSkeletonPath(path);
				}
				catch (const std::exception&)
				{
					// A container that will not read tells the backfill nothing; the walk below
					// reports it per-file.
				}
			}

			for (auto& entry : facts) std::ranges::sort(entry.second.outputs);
			return facts;
		}
	}

	size_t
	MigrateReport::Count(MigratedFile::Outcome outcome) const noexcept
	{
		return static_cast<size_t>(std::ranges::count(files, outcome, &MigratedFile::outcome));
	}

	MigrateReport
	AssetStore::Migrate(bool dryRun) const
	{
		std::vector<std::filesystem::path> paths;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(GetDataRoot()))
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

		MigrateReport report;

		// Before everything: the walk below regenerates through documents that must already name
		// their rig, and a project written before that field existed has none.
		const auto facts = factsFromDerived(*this, paths);
		for (const std::string& documentKey : GetFiles().Enumerate(c_MeshesSrcDirectoryName))
		{
			if (extensionOf(documentKey) != c_ImportDocumentExtension)
				continue;

			const std::filesystem::path documentPath = GetDataRoot() / documentKey;
			MigratedFile                file{ documentPath, MigratedFile::Outcome::kUnchanged, {} };
			try
			{
				const auto found = facts.find(importedSourceKeyFor(documentKey));
				if (found == facts.end())
					continue;

				ImportDocument       document = loadImportDocument(GetFiles(), documentKey);
				const ImportDocument before   = document;

				if (document.skeleton.empty())
					document.skeleton = found->second.skeleton;
				if (document.outputs.empty())
					document.outputs = found->second.outputs;

				// A source with no rig has no skeleton to record, so "still empty" is settled
				// rather than pending; only a real change may report one.
				if (document == before)
					continue;

				if (!dryRun)
					writeFileBytes(
						documentPath,
						AssetCodec<ImportDocument>::Serialize(document),
						"migrate");
				file.outcome = MigratedFile::Outcome::kRewritten;
			}
			catch (const std::exception& error)
			{
				file.outcome = MigratedFile::Outcome::kFailed;
				file.message = error.what();
			}

			if (file.outcome != MigratedFile::Outcome::kUnchanged)
				report.files.push_back(std::move(file));
		}

		// Then, before the walk: what the sources say should stand but does not. The walk below
		// re-saves files it finds; only this puts an absent one back.
		for (const ReimportedSource& source : Reimport(dryRun).sources)
		{
			if (!source.message.empty())
			{
				report.files.push_back(
					{ GetDataRoot() / source.source,
				      MigratedFile::Outcome::kFailed,
				      source.message });
				continue;
			}
			for (const std::string& output : source.written)
				report.files.push_back(
					{ GetDataRoot() / output, MigratedFile::Outcome::kRewritten, {} });
		}

		// Before the walk: a refresh stamps the `.bimport` the walk then reads.
		for (const std::string& source : GetStaleImportedTextureSources())
		{
			const std::filesystem::path documentPath = GetDataRoot() / importDocumentKeyFor(source);

			MigratedFile file{ documentPath, MigratedFile::Outcome::kRewritten, {} };
			if (dryRun)
			{
				report.files.push_back(std::move(file));
				continue;
			}

			try
			{
				const TextureRefresh refresh = RefreshImportedTextures(source);
				for (const std::string& written : refresh.written)
					report.files.push_back(
						{ GetDataRoot() / written, MigratedFile::Outcome::kRewritten, {} });
				report.supersededTextures.insert(
					report.supersededTextures.end(),
					refresh.superseded.begin(),
					refresh.superseded.end());
				report.movedTextures.insert(
					report.movedTextures.end(),
					refresh.moved.begin(),
					refresh.moved.end());
			}
			catch (const std::exception& error)
			{
				file.outcome = MigratedFile::Outcome::kFailed;
				file.message = error.what();
			}
			report.files.push_back(std::move(file));
		}

		std::ranges::sort(report.supersededTextures);
		std::ranges::sort(report.movedTextures, {}, &MovedTexture::from);

		// Last word on the textures: a material naming one that is not there draws untextured and
		// says nothing about it, which is the failure that is hardest to see and easiest to ship.
		for (const std::filesystem::path& path : paths)
		{
			if (assetTypeFromExtension(path) != AssetType::kMaterial)
				continue;

			const std::string key =
				normalizeRef(path.lexically_relative(GetDataRoot()).generic_string());
			try
			{
				BMaterial material = Load<BMaterial>(key);
				for (const auto& [texture, kind] :
				     mapMaterialTextures(material, [](const std::string& k) { return k; }))
					if (!Exists(texture))
						report.danglingTextures.push_back(key + ": " + texture);
			}
			catch (const std::exception&)
			{
				// Unreadable is the walk below's to report, with the reason.
			}
		}
		std::ranges::sort(report.danglingTextures);
		const auto repeats = std::ranges::unique(report.danglingTextures);
		report.danglingTextures.erase(repeats.begin(), repeats.end());

		// The walk below was listed before the refresh ran, and a followed texture is no longer at
		// the path it was listed under. Reading it would report a file this call itself moved.
		std::erase_if(paths, [&](const std::filesystem::path& path) {
			const std::string key =
				normalizeRef(path.lexically_relative(GetDataRoot()).generic_string());
			return std::ranges::any_of(report.movedTextures, [&](const MovedTexture& moved) {
				return moved.from == key;
			});
		});

		for (const std::filesystem::path& path : paths)
		{
			const auto type = assetTypeFromExtension(path);
			if (!type)
				continue;

			MigratedFile file{ path, MigratedFile::Outcome::kUnchanged, {} };
			try
			{
				const std::string key =
					normalizeRef(path.lexically_relative(GetDataRoot()).generic_string());

				const auto bytes   = core::file::read_file_bytes(path.string());
				const auto current = resave(*this, *type, key, bytes);
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
