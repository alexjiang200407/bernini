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
		constexpr std::array<AssetType, 3> c_Order = { AssetType::kSkeleton,
			                                           AssetType::kMesh,
			                                           AssetType::kAnimation };

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

				static_cast<void>(applyBindings(mesh, document.bindings));
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
					bakeBoundsForRig(clips, store.GetDataRoot(), normalizeRef(clips.skeleton), rig);
				}
				else
				{
					BMesh swept = toBMesh(group.import);
					generateTangents(swept);
					swept.skeleton = document.skeleton;
					bakePosedBounds(clips, swept, rig);
				}

				store.Save(clips, key);
				return;
			}
			default:
				core::throw_runtime_error("'{}' is not a container an import produces", key);
			}
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
	ReimportReport::WrittenCount() const noexcept
	{
		size_t total = 0;
		for (const ReimportedSource& source : sources) total += source.written.size();
		return total;
	}

	size_t
	ReimportReport::FailedCount() const noexcept
	{
		return static_cast<size_t>(
			std::ranges::count_if(sources, [](const ReimportedSource& source) {
				return !source.message.empty();
			}));
	}

	ReimportReport
	AssetStore::Reimport(bool dryRun) const
	{
		auto pending = std::vector<PendingSource>();
		for (const std::string& documentKey : GetFiles().Enumerate(c_MeshesSrcDirectoryName))
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
					written.erase(source.key);
				}
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
			if (broke != failed.end())
				entry.message = broke->second;
			else
				entry.written = wrote->second;

			std::ranges::sort(entry.written);
			report.sources.push_back(std::move(entry));
		}
		return report;
	}
}
