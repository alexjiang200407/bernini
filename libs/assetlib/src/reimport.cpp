#include <assetlib/codecs.h>
#include <assetlib/reimport.h>

#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "import_bounds.h"
#include "ref_paths.h"
#include "regen_group.h"

#include <core/err/util.h>

namespace assetlib
{
	namespace
	{
		/** One source, its document, and the outputs the document claims. */
		struct PendingSource
		{
			std::string    key;
			ImportDocument document;
		};

		/**
		 * Rigs before meshes before clips: a mesh names the rig it binds, and a clip set sweeps its
		 * boxes through the meshes standing on disk.
		 */
		constexpr std::array<AssetType, 3> c_Order = {
			{ AssetType::kSkeleton, AssetType::kMesh, AssetType::kAnimation }
		};

		/** The `.bmesh` among a document's outputs, or empty for a clips-only source. */
		std::string
		meshOutputOf(const ImportDocument& document)
		{
			for (const std::string& output : document.outputs)
				if (assetTypeFromExtension(output) == AssetType::kMesh)
					return output;
			return {};
		}

		void
		writeOutput(
			const AssetStore&       store,
			const RegeneratedGroup& group,
			const ImportDocument&   document,
			AssetType               type,
			std::string_view        key)
		{
			switch (type)
			{
			case AssetType::kSkeleton:
			{
				Skeleton rig = group.import.skeleton;
				rig.source   = group.ref;
				store.Save(rig, key);
				return;
			}
			case AssetType::kMesh:
			{
				BMesh mesh = toBMesh(group.import);
				generateTangents(mesh);
				requireUniqueSubmeshNames(mesh);
				mesh.source = group.ref;

				if (isSkinned(mesh))
				{
					core::throw_runtime_error_if(
						document.skeleton.empty(),
						"'{}': its source carries a rig but the import document names no "
						"skeleton; run `assetlib_cli migrate` to record the one it already uses",
						key);
					mesh.skeleton          = document.skeleton;
					mesh.skeletonSignature = skeletonSignature(group.import.skeleton);
				}

				static_cast<void>(applyDocument(mesh, document));
				store.Save(mesh, key);
				return;
			}
			case AssetType::kAnimation:
			{
				core::throw_runtime_error_if(
					group.import.animations.clips.empty(),
					"'{}': its source no longer carries clips",
					key);
				core::throw_runtime_error_if(
					document.skeleton.empty(),
					"'{}': its import document names no skeleton, so which rig its clips address "
					"cannot be known; run `assetlib_cli migrate` to record it",
					key);

				AnimationSet   clips = group.import.animations;
				const Skeleton rig   = store.Load<Skeleton>(document.skeleton);
				clips.source         = group.ref;
				clips.skeleton       = document.skeleton;

				// Whichever writer produced these outputs is the one reproduced, because anything
				// else makes a re-import distinguishable from an import. A source that produced a
				// mesh swept that mesh (WriteImportedRig, which had it in hand and not yet on
				// disk); a clips-only source had none and swept the project's
				// (WriteImportedClips). Re-measuring project-wide is `bakebounds`, deliberately
				// its own operation.
				if (const std::string mesh = meshOutputOf(document); mesh.empty())
				{
					bakeBoundsForRig(
						store,
						clips,
						normalizeRef(clips.skeleton),
						rig,
						document.clipFloors);
				}
				else
				{
					BMesh swept = toBMesh(group.import);
					generateTangents(swept);
					swept.skeleton = document.skeleton;
					groundClips(clips, std::span<const BMesh>(&swept, 1), rig, document.clipFloors);
					bakePosedBounds(clips, swept, rig);
				}

				store.Save(clips, key);
				return;
			}
			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kEnvironment:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kVat:
			case AssetType::kImportDocument:
			case AssetType::kCount:
				break;
			}
			core::throw_runtime_error("'{}' is not a container an import produces", key);
		}

		/**
		 * Absent, and only absent. A container that is on disk but stale is `Migrate`'s -- it can
		 * read one and re-save it, which is cheaper than a re-import and is the operation that
		 * already exists. Splitting them this way is also what keeps the two from reporting one
		 * problem twice when Migrate runs both.
		 */
		bool
		wanted(const AssetStore& store, std::string_view key)
		{
			return !store.Exists(key);
		}
	}

	size_t
	ReimportReport::GetWrittenCount() const noexcept
	{
		size_t total = 0;
		for (const ReimportedSource& source : sources) total += source.written.size();
		return total;
	}

	size_t
	ReimportReport::GetFailedCount() const noexcept
	{
		return static_cast<size_t>(
			std::ranges::count_if(sources, [](const ReimportedSource& source) {
				return !source.message.empty();
			}));
	}

	std::vector<std::string>
	AssetStore::GetStaleGeometry() const
	{
		if (IsReadOnly())
			return {};

		auto stale = std::vector<std::string>();
		for (const auto& entry : std::filesystem::recursive_directory_iterator(GetDataRoot()))
		{
			if (!entry.is_regular_file())
				continue;

			const auto type = assetTypeFromExtension(entry.path());
			if (!type.has_value() || !isGeometryContainer(*type))
				continue;

			const std::string key =
				normalizeRef(entry.path().lexically_relative(GetDataRoot()).generic_string());
			try
			{
				if (GeometryIsStale(key))
					stale.push_back(key);
			}
			catch (const std::exception&)
			{
				// A header that will not read is the asset scan's to report, not this scan's --
				// which exists to say whether the project needs updating, not what is broken.
			}
		}

		std::ranges::sort(stale);
		return stale;
	}

	ReimportReport
	AssetStore::Reimport(bool dryRun) const
	{
		auto pending = std::vector<PendingSource>();
		for (const std::string& documentKey : GetFiles().Enumerate(c_MeshSourcesDirectoryName))
		{
			if (extensionOf(documentKey) != c_ImportDocumentExtension)
				continue;
			pending.push_back(
				{ importedSourceKeyFor(documentKey), loadImportDocument(GetFiles(), documentKey) });
		}
		std::ranges::sort(pending, {}, &PendingSource::key);

		auto written = std::unordered_map<std::string, std::vector<std::string>>();
		auto failed  = std::unordered_map<std::string, std::string>();

		for (const AssetType type : c_Order)
		{
			for (const PendingSource& source : pending)
			{
				if (failed.contains(source.key))
					continue;

				auto todo = std::vector<std::string>();
				for (const std::string& output : source.document.outputs)
					if (assetTypeFromExtension(output) == type && wanted(*this, output))
						todo.push_back(output);

				if (todo.empty())
					continue;

				if (dryRun)
				{
					auto& list = written[source.key];
					list.insert(list.end(), todo.begin(), todo.end());
					continue;
				}

				try
				{
					core::throw_runtime_error_if(
						!Exists(source.key),
						"'{}' is not in the project, so nothing can be produced from it",
						source.key);

					// Parsed once per kind rather than held across all three: a source's meshes are
					// the largest thing in this library, and every one of them would otherwise stay
					// resident until the last clip set was baked.
					const RegeneratedGroup group =
						importGroup(*this, source.key, ImportDocument(source.document));

					for (const std::string& output : todo)
					{
						writeOutput(*this, group, source.document, type, output);
						written[source.key].push_back(output);
					}
				}
				catch (const std::exception& error)
				{
					failed.emplace(source.key, error.what());
				}
			}
		}

		// The extracted textures are the one output no `outputs` entry names -- a `.ktx2` carries
		// no header, so the document's textureDir and textureStamp are their whole key, and that
		// key says nothing about whether the files are on disk. An empty or absent folder is the
		// only signal there is, and it is exactly the fresh-checkout case.
		for (const PendingSource& source : pending)
		{
			if (failed.contains(source.key) || source.document.textureDir.empty())
				continue;

			const std::filesystem::path folder = GetDataRoot() / source.document.textureDir;
			if (std::filesystem::exists(folder) && !std::filesystem::is_empty(folder))
				continue;

			if (dryRun)
			{
				written[source.key].push_back(source.document.textureDir);
				continue;
			}

			try
			{
				for (const std::string& file : RefreshImportedTextures(source.key).written)
					written[source.key].push_back(file);
			}
			catch (const std::exception& error)
			{
				failed.emplace(source.key, error.what());
			}
		}

		ReimportReport report;
		for (const PendingSource& source : pending)
		{
			const auto wrote = written.find(source.key);
			const auto broke = failed.find(source.key);
			if (wrote == written.end() && broke == failed.end())
				continue;

			ReimportedSource entry{ source.key, {}, {} };
			if (wrote != written.end())
				entry.written = wrote->second;
			if (broke != failed.end())
				entry.message = broke->second;

			std::ranges::sort(entry.written);
			report.sources.push_back(std::move(entry));
		}
		return report;
	}
}
