#include <algorithm>
#include <array>
#include <assetlib/codecs.h>
#include <assetlib/reimport.h>

#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/project_layout.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>

#include "cook_threads.h"
#include "import_bounds.h"
#include "plant_bake.h"
#include "progress_report.h"
#include "ref_paths.h"
#include "regen_group.h"
#include <assetlib/progress.h>
#include <atomic>
#include <core/parallel_for.h>

#include <core/err/util.h>
#include <core/str/str.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

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

		/**
		 * A source's outputs of one type that this run has to produce, and where in `pending` the
		 * source is. Decided before any of it runs, so the count the sink reports against is fixed
		 * and a stage's items can be handed out in any order.
		 */
		struct StageItem
		{
			size_t                   source = 0;
			std::vector<std::string> outputs;
		};

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
				if (const std::string mesh = document.GetMeshOutput(); mesh.empty())
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
					groundClipsForRig(
						store.GetFiles(),
						clips,
						std::span<const BMesh>(&swept, 1),
						rig,
						document.clipFloors);
					bakePosedBounds(clips, swept, rig);
					bakePlantWeightsForRig(
						store.GetFiles(),
						clips,
						std::span<const BMesh>(&swept, 1),
						rig);
				}

				store.Save(clips, key);
				return;
			}
			case AssetType::kMaterial:
			case AssetType::kTexture:
			case AssetType::kEnvironment:
			case AssetType::kSky:
			case AssetType::kEnvLighting:
			case AssetType::kImportDocument:
			case AssetType::kUiDocument:
			case AssetType::kUiStyle:
			case AssetType::kFont:
			case AssetType::kAvatar:
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
		ZoneScopedN("assetlib scan stale geometry");

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
	AssetStore::Reimport(bool dryRun, const ProgressSink& onProgress) const
	{
		ZoneScopedN("assetlib reimport");
		ZoneTextF("%s", dryRun ? "dry run" : "writing");

		const ProgressSink sink = serialized(onProgress);

		auto pending = std::vector<PendingSource>();
		for (const std::string& documentKey : GetFiles().Enumerate(c_MeshSourcesDirectoryName))
		{
			if (extensionOf(documentKey) != c_ImportDocumentExtension)
				continue;
			pending.push_back(
				{ importedSourceKeyFor(documentKey), loadImportDocument(GetFiles(), documentKey) });
		}
		std::ranges::sort(pending, {}, &PendingSource::key);

		// The extracted textures are the one output no `outputs` entry names -- a `.ktx2` carries
		// no header, so the document's textureDir and textureStamp are their whole key, and that
		// key says nothing about whether the files are on disk. An empty or absent folder is the
		// only signal there is, and it is exactly the fresh-checkout case.
		const auto textureWanted = [this](const PendingSource& source) {
			if (source.document.textureDir.empty())
				return false;

			const std::filesystem::path folder = GetDataRoot() / source.document.textureDir;
			return !std::filesystem::exists(folder) || std::filesystem::is_empty(folder);
		};

		auto   stages   = std::array<std::vector<StageItem>, c_Order.size()>();
		auto   textures = std::vector<size_t>();
		size_t total    = 0;

		// Deciding the work up front is what fixes the count, but it also means `wanted` is asked
		// before any of it runs -- where the walk it replaced re-asked per source and so could
		// never hand the same output to two of them. A rig reused by a second source is normally
		// left out of that source's `outputs` for exactly this reason; a project where it was not
		// would otherwise have two threads writing one file with two different `source` fields.
		auto claimed = std::unordered_set<std::string>();

		for (size_t stage = 0; stage < c_Order.size(); ++stage)
			for (size_t i = 0; i < pending.size(); ++i)
			{
				auto outputs = std::vector<std::string>();
				for (const std::string& output : pending[i].document.outputs)
					if (assetTypeFromExtension(output) == c_Order[stage] && wanted(*this, output) &&
					    claimed.insert(output).second)
						outputs.push_back(output);

				if (outputs.empty())
					continue;

				total += outputs.size();
				stages[stage].push_back({ i, std::move(outputs) });
			}

		for (size_t i = 0; i < pending.size(); ++i)
			if (textureWanted(pending[i]))
			{
				++total;
				textures.push_back(i);
			}

		auto written = core::str::unordered_str_map<std::vector<std::string>>();
		auto failed  = core::str::unordered_str_map<std::string>();
		auto guard   = std::mutex();
		auto done    = std::atomic<size_t>(0);

		if (dryRun)
		{
			for (const std::vector<StageItem>& stage : stages)
				for (const StageItem& item : stage)
				{
					std::vector<std::string>& list = written[pending[item.source].key];
					list.insert(list.end(), item.outputs.begin(), item.outputs.end());
				}

			for (const size_t i : textures)
				written[pending[i].key].push_back(pending[i].document.textureDir);
		}
		else
		{
			// A stage at a time, because a mesh names the rig it binds and a clip set sweeps its
			// boxes through the meshes standing on disk. Within one, the sources are independent.
			for (size_t stage = 0; stage < c_Order.size(); ++stage)
			{
				const AssetType type = c_Order[stage];

				core::parallel_for(
					stages[stage].size(),
					c_MaxCookThreads,
					c_CookThreadName,
					[&](size_t index) {
						const StageItem&     item   = stages[stage][index];
						const PendingSource& source = pending[item.source];

						{
							const auto held = std::lock_guard(guard);
							if (failed.contains(source.key))
								return;
						}

						try
						{
							core::throw_runtime_error_if(
								!Exists(source.key),
								"'{}' is not in the project, so nothing can be produced from it",
								source.key);

							// Parsed once per kind rather than held across all three: a source's
							// meshes are the largest thing in this library, and every one of them
							// would otherwise stay resident until the last clip set was baked.
							const RegeneratedGroup group =
								importGroup(*this, source.key, ImportDocument(source.document));

							for (const std::string& output : item.outputs)
							{
								reportStep(
									sink,
									ProgressPhase::kRegenerating,
									output,
									done.fetch_add(1),
									total);

								writeOutput(*this, group, source.document, type, output);

								const auto held = std::lock_guard(guard);
								written[source.key].push_back(output);
							}
						}
						catch (const std::exception& error)
						{
							const auto held = std::lock_guard(guard);
							failed.emplace(source.key, error.what());
						}
					});
			}

			core::parallel_for(
				textures.size(),
				c_MaxCookThreads,
				c_CookThreadName,
				[&](size_t index) {
					const PendingSource& source = pending[textures[index]];

					{
						const auto held = std::lock_guard(guard);
						if (failed.contains(source.key))
							return;
					}

					const size_t at = done.fetch_add(1);
					reportStep(sink, ProgressPhase::kExtractingTextures, source.key, at, total);

					try
					{
						// The inner sink names the image being encoded without moving the bar: this
						// whole extract is one of `total`, however many files it turns out to write.
						const TextureRefresh refresh =
							RefreshImportedTextures(source.key, [&](const ProgressEvent& event) {
								reportStep(
									sink,
									ProgressPhase::kExtractingTextures,
									event.subject,
									at,
									total);
							});

						const auto held = std::lock_guard(guard);
						for (const std::string& file : refresh.written)
							written[source.key].push_back(file);
					}
					catch (const std::exception& error)
					{
						const auto held = std::lock_guard(guard);
						failed.emplace(source.key, error.what());
					}
				});
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
