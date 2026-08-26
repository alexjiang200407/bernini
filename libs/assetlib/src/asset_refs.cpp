#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/vat_bake.h>

#include <assetlib/AssetCodec.h>

#include <assetlib/import_document.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>

#include <core/file/LooseFileSystem.h>

#include "ref_paths.h"

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		std::string
		lowerExtension(const std::filesystem::path& path)
		{
			std::string ext = path.extension().string();
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return ext;
		}

		void
		addEdge(
			std::vector<AssetRef>& edges,
			const std::string&     referrer,
			const std::string&     target,
			RefKind                kind)
		{
			if (target.empty())
				return;

			edges.push_back(AssetRef{ referrer, normalizeRef(target), kind });
		}

		/** Every material a `.bmesh` names, in `mesh.materials` order, and the skeleton it skins to. */
		void
		collectMeshEdges(
			std::vector<AssetRef>& edges,
			const AssetStore&      store,
			const std::string&     referrer)
		{
			MeshRefs refs;
			try
			{
				refs = store.LoadRegenMeshRefs(referrer);
			}
			catch (const std::exception& e)
			{
				// Fatal, as it is for the prune: a mesh we cannot read is a mesh whose materials we cannot
				// see, and we would then delete one of them out from under it.
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the mesh '" + referrer +
					"', so the assets it references cannot be known: " + e.what());
			}

			for (const std::string& material : refs.materials)
				addEdge(edges, referrer, material, RefKind::kSubmeshMaterial);

			addEdge(edges, referrer, refs.skeleton, RefKind::kMeshSkeleton);
		}

		/** The skeleton a `.banim`'s clips were resampled against. */
		void
		collectAnimationEdges(
			std::vector<AssetRef>& edges,
			const AssetStore&      store,
			const std::string&     referrer)
		{
			std::string skeleton;
			try
			{
				skeleton = store.LoadRegenAnimationSkeletonPath(referrer);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the clip set '" + referrer +
					"', so the skeleton it references cannot be known: " + e.what());
			}

			addEdge(edges, referrer, skeleton, RefKind::kClipSkeleton);
		}

		/**
		 * The document holds its source, every material its bindings name, the rig it binds and
		 * every container it produced. The last two are references like any other: nothing else
		 * records them, so a rename that missed one would leave the document naming a file that is
		 * gone -- and an `outputs` entry naming a key that no longer exists reads as *absent* to
		 * the producing side, which would put the old file back.
		 */
		void
		collectImportDocumentEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			const ImportDocument document = loadImportDocument(files, referrer);
			addEdge(edges, referrer, importedSourceKeyFor(referrer), RefKind::kImportedSource);
			for (const MaterialBinding& binding : document.bindings)
				addEdge(edges, referrer, binding.material, RefKind::kSubmeshMaterial);

			if (!document.skeleton.empty())
				addEdge(edges, referrer, document.skeleton, RefKind::kDocumentSkeleton);
			for (const std::string& output : document.outputs)
				addEdge(edges, referrer, output, RefKind::kDocumentOutput);
		}

		/** The three inputs a `.bvat` was baked from -- what a re-bake reads. */
		void
		collectVatEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			VatRefs refs;
			try
			{
				refs = loadVatRefs(files, referrer);
			}
			catch (const std::exception&)
			{
				// Skipped rather than fatal, unlike every other referrer here -- see Scan's docstring.
				return;
			}

			addEdge(edges, referrer, refs.mesh, RefKind::kVatSource);
			addEdge(edges, referrer, refs.skeleton, RefKind::kVatSource);
			addEdge(edges, referrer, refs.animations, RefKind::kVatSource);
		}

		/** The baked triplet a `.bmaterial` names, and the sources its channels route from. */
		void
		collectMaterialEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			auto material = BMaterial();
			try
			{
				material = load<BMaterial>(files, referrer);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the material '" + referrer +
					"', so the textures it references cannot be known: " + e.what());
			}

			switch (material.shadingModel)
			{
			case ShadingModel::kPbr:
				addEdge(edges, referrer, material.pbr.baseColorTexture, RefKind::kBakedMap);
				addEdge(edges, referrer, material.pbr.normalTexture, RefKind::kBakedMap);
				addEdge(edges, referrer, material.pbr.ormTexture, RefKind::kBakedMap);

				// A material names its textures twice: the triplet its last bake wrote, and the sources it
				// routes each channel from. Both hold a file alive -- the sources are what a re-bake reads.
				for (const ChannelRoute& route : material.pbr.routes)
					addEdge(edges, referrer, route.texture, RefKind::kChannelRoute);
				break;

			case ShadingModel::kCount:
				throw std::runtime_error(
					"assetlib::AssetRefGraph: the material '" + referrer +
					"' names an unknown shading model, so its textures cannot be known");
			}
		}

		// A route holds two files alive for the same reason a material's does: the baked map is what the
		// renderer samples, and the source is what a re-bake reads.
		void
		addRouteEdges(
			std::vector<AssetRef>& edges,
			const std::string&     referrer,
			const EnvMapRoute&     route)
		{
			addEdge(edges, referrer, route.baked, RefKind::kBakedMap);
			addEdge(edges, referrer, route.source, RefKind::kEnvSource);
		}

		/** The radiance a `.bsky` routes, and the map its bake wrote. */
		void
		collectSkyEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			try
			{
				addRouteEdges(edges, referrer, load<BSky>(files, referrer).sky);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the sky '" + referrer +
					"', so the textures it references cannot be known: " + e.what());
			}
		}

		/** Both halves of a `.benvl`: each names a source and the map convolved from it. */
		void
		collectEnvLightingEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			try
			{
				const BEnvLighting lighting = load<BEnvLighting>(files, referrer);
				addRouteEdges(edges, referrer, lighting.prefilter);
				addRouteEdges(edges, referrer, lighting.irradiance);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the environment lighting '" + referrer +
					"', so the textures it references cannot be known: " + e.what());
			}
		}

		/** The pair a `.benv` composes. It holds no pixels, so these are its only edges. */
		void
		collectEnvironmentEdges(
			std::vector<AssetRef>&         edges,
			const core::file::IFileSystem& files,
			const std::string&             referrer)
		{
			try
			{
				const BEnv env = load<BEnv>(files, referrer);
				addEdge(edges, referrer, env.sky, RefKind::kEnvironmentPart);
				addEdge(edges, referrer, env.lighting, RefKind::kEnvironmentPart);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the environment '" + referrer +
					"', so the assets it composes cannot be known: " + e.what());
			}
		}
	}

	std::optional<AssetType>
	assetTypeFromExtension(const std::filesystem::path& path)
	{
		const std::string ext = lowerExtension(path);

		// The one asset with no codec, so the one the table cannot answer for: a texture is an
		// image this library encodes, not a container it serializes a struct into.
		if (ext == c_TextureExtension)
			return AssetType::kTexture;

		const std::optional<ContainerKind> kind = containerKindForExtension(ext);
		if (!kind.has_value())
			return std::nullopt;

		return kind->type;
	}

	bool
	AssetRefGraph::Contains(std::string_view path) const
	{
		return std::ranges::binary_search(m_Files, normalizeRef(path));
	}

	std::vector<std::string>
	AssetRefGraph::GetFilesUnder(std::string_view directory) const
	{
		// Normalized like every other query here, and then stripped of a trailing separator: a
		// directory named with one would otherwise build a `//` prefix and match nothing at all.
		std::string prefix = normalizeRef(directory);
		while (prefix.ends_with('/')) prefix.pop_back();
		prefix += '/';

		const auto first = std::ranges::lower_bound(m_Files, prefix);
		const auto last  = std::ranges::partition_point(
			std::ranges::subrange(first, m_Files.end()),
			[&prefix](const std::string& path) { return path.starts_with(prefix); });

		return std::vector<std::string>(first, last);
	}

	AssetRefGraph
	AssetRefGraph::Scan(const AssetStore& store)
	{
		const core::file::IFileSystem& files = store.GetFiles();

		auto graph       = AssetRefGraph();
		graph.m_DataRoot = store.GetDataRoot();
		graph.m_Files    = files.Enumerate();
		std::ranges::sort(graph.m_Files);

		auto edges = std::vector<AssetRef>();

		for (const std::string& referrer : graph.m_Files)
		{
			const std::string kind = extensionOf(referrer);

			if (kind == c_MeshExtension)
			{
				collectMeshEdges(edges, store, referrer);
				++graph.meshesScanned;
			}
			else if (kind == c_MaterialExtension)
			{
				collectMaterialEdges(edges, files, referrer);
				++graph.materialsScanned;
			}
			else if (kind == c_SkyExtension)
			{
				collectSkyEdges(edges, files, referrer);
				++graph.environmentsScanned;
			}
			else if (kind == c_EnvLightingExtension)
			{
				collectEnvLightingEdges(edges, files, referrer);
				++graph.environmentsScanned;
			}
			else if (kind == c_EnvironmentExtension)
			{
				collectEnvironmentEdges(edges, files, referrer);
				++graph.environmentsScanned;
			}
			else if (kind == c_AnimationExtension)
			{
				collectAnimationEdges(edges, store, referrer);
				++graph.clipSetsScanned;
			}
			else if (kind == c_VatExtension)
			{
				collectVatEdges(edges, files, referrer);
				++graph.vatsScanned;
			}
			else if (kind == c_ImportDocumentExtension)
			{
				collectImportDocumentEdges(edges, files, referrer);
				++graph.importDocumentsScanned;
			}
		}

		// A mesh may name one material from two submesh slots, and a material may route two channels from
		// one texture. Either way it is one referrer, and must be reported to the user once.
		std::ranges::sort(edges, [](const AssetRef& a, const AssetRef& b) {
			return std::tie(a.target, a.referrer, a.kind) < std::tie(b.target, b.referrer, b.kind);
		});
		const auto duplicates = std::ranges::unique(edges);
		edges.erase(duplicates.begin(), duplicates.end());

		graph.m_Edges = std::move(edges);

		for (uint32_t i = 0; i < graph.m_Edges.size();)
		{
			const std::string& target = graph.m_Edges[i].target;

			uint32_t end = i;
			while (end < graph.m_Edges.size() && graph.m_Edges[end].target == target) ++end;

			graph.m_ByTarget.emplace(target, Range{ i, end - i });

			if (!graph.Contains(target))
				graph.broken.insert(
					graph.broken.end(),
					graph.m_Edges.begin() + i,
					graph.m_Edges.begin() + end);

			i = end;
		}

		return graph;
	}

	std::span<const AssetRef>
	AssetRefGraph::ReferrersOf(std::string_view asset) const
	{
		const auto it = m_ByTarget.find(normalizeRef(asset));
		if (it == m_ByTarget.end())
			return {};

		return std::span(m_Edges).subspan(it->second.first, it->second.count);
	}

	std::vector<AssetRef>
	AssetRefGraph::ReferencesOf(std::string_view asset) const
	{
		const std::string key = normalizeRef(asset);

		auto out = std::vector<AssetRef>();
		for (const AssetRef& edge : m_Edges)
			if (edge.referrer == key)
				out.push_back(edge);

		return out;
	}

	std::vector<AssetRef>
	AssetRefGraph::ReferrersInto(std::string_view directory) const
	{
		const std::string dir = normalizeRef(directory);

		auto out = std::vector<AssetRef>();
		for (const AssetRef& edge : m_Edges)
			if (isUnder(edge.target, dir) && !isUnder(edge.referrer, dir))
				out.push_back(edge);

		return out;
	}

	namespace
	{
		/**
		 * The closure of what deleting `plan`'s target frees: every asset the deleted set references
		 * whose every referrer is itself in the set, repeated until nothing more qualifies -- a
		 * material freed by its last mesh frees the textures it alone routed.
		 */
		std::vector<std::string>
		cascadeOf(const AssetRefGraph& graph, const DeletionPlan& plan)
		{
			auto deleted = std::unordered_set<std::string>();
			if (plan.IsDirectory())
				deleted.insert(plan.contents.begin(), plan.contents.end());
			else
				deleted.insert(plan.target);

			// The swept bakes go too, so what only they referenced can come free.
			deleted.insert(plan.derived.begin(), plan.derived.end());

			auto cascade = std::vector<std::string>();

			bool grew = true;
			while (grew)
			{
				grew = false;
				for (const AssetRef& edge : graph.Edges())
				{
					if (!deleted.contains(edge.referrer) || deleted.contains(edge.target))
						continue;

					const std::span<const AssetRef> holders = graph.ReferrersOf(edge.target);
					if (!std::ranges::all_of(holders, [&](const AssetRef& holder) {
							return deleted.contains(holder.referrer);
						}))
						continue;

					// A target the scan did not see is a broken edge, not something to delete.
					if (!graph.Contains(edge.target))
						continue;

					cascade.push_back(edge.target);
					deleted.insert(edge.target);
					grew = true;
				}
			}

			std::ranges::sort(cascade);
			return cascade;
		}
	}

	DeletionPlan
	planDeletion(const AssetRefGraph& graph, std::string_view target)
	{
		auto plan   = DeletionPlan();
		plan.target = normalizeRef(target);

		requireInsideDataRoot("assetlib::planDeletion", plan.target);

		// A directory is not an asset, and has no kind: that is what nullopt says.
		//
		// A mount enumerates files, so an *empty* directory is invisible to the scan. It still
		// exists on the writable layer, and that is the only layer one can be removed from -- so the
		// disk answers for that case alone, the way the prune's sweep does.
		auto referrers = std::vector<AssetRef>();

		std::vector<std::string> contents = graph.GetFilesUnder(plan.target);
		if (!contents.empty() || std::filesystem::is_directory(graph.DataRoot() / plan.target))
		{
			plan.contents = std::move(contents);
			referrers     = graph.ReferrersInto(plan.target);
		}
		else
		{
			plan.assetType = assetTypeFromExtension(plan.target);
			if (!plan.assetType)
				throw std::runtime_error(
					"assetlib::planDeletion: '" + plan.target +
					"' is not an asset this project stores anything about");

			const std::span<const AssetRef> held = graph.ReferrersOf(plan.target);
			referrers.assign(held.begin(), held.end());
		}

		// A bake's edges sweep the bake rather than block: see DeletionPlan::derived.
		for (const AssetRef& ref : referrers)
		{
			if (ref.kind == RefKind::kVatSource)
				plan.derived.push_back(ref.referrer);
			else
				plan.blockers.push_back(ref);
		}
		std::ranges::sort(plan.derived);
		plan.derived.erase(std::ranges::unique(plan.derived).begin(), plan.derived.end());

		return plan;
	}

	DeletionPlan
	planCascadeDeletion(const AssetRefGraph& graph, std::string_view target)
	{
		DeletionPlan plan = planDeletion(graph, target);

		// A blocked deletion frees nothing, so there is no cascade to compute for one.
		if (plan.Allowed())
			plan.cascade = cascadeOf(graph, plan);

		return plan;
	}

	DeletionResult
	AssetStore::DeleteAsset(const DeletionPlan& plan) const
	{
		if (!plan.Allowed())
			return DeletionResult{ DeletionStatus::kRefused, {} };

		const std::filesystem::path path = GetDataRoot() / plan.target;

		// A plan can name assets only the archive holds -- the target, a member of the directory it
		// names, something its cascade frees, or a bake derived from it. `remove` reports no error
		// for a path that was never there, so each would come back kDeleted having touched nothing
		// while staying readable through the mount. Task 10's tombstone is what answers this.
		//
		// Inert on a loose store, where the mount is the data root: what is not on disk is not in
		// the mount either.
		const auto onlyInMount = [this](const std::string& key) {
			return !std::filesystem::exists(GetDataRoot() / key) && Exists(key);
		};

		auto unreachable = std::vector<std::string>();

		if (plan.IsDirectory())
			std::ranges::copy_if(plan.contents, std::back_inserter(unreachable), onlyInMount);
		else if (onlyInMount(plan.target))
			unreachable.push_back(plan.target);

		std::ranges::copy_if(plan.cascade, std::back_inserter(unreachable), onlyInMount);
		std::ranges::copy_if(plan.derived, std::back_inserter(unreachable), onlyInMount);

		if (!unreachable.empty())
			return DeletionResult{ DeletionStatus::kFailed,
				                   "'" + unreachable.front() +
				                       "' is only in a read-only mount, so it cannot be unlinked" };

		// The bakes first: they reference the target, so no failure below leaves one standing on
		// inputs that are gone. Already-vanished ones count as deleted, as the target itself does.
		std::error_code ec;
		for (const std::string& bake : plan.derived)
		{
			std::filesystem::remove(GetDataRoot() / bake, ec);
			if (ec)
				return DeletionResult{ DeletionStatus::kFailed, ec.message() };
		}

		if (plan.IsDirectory())
			std::filesystem::remove_all(path, ec);
		else
			std::filesystem::remove(path, ec);

		// Something already gone is the outcome the caller wanted, not a failure -- the user may well have
		// deleted it from a file manager since the scan. A directory that came off part-way is a failure,
		// and is reported as one even though some of it is now gone; there is no undo to offer instead.
		if (ec)
			return DeletionResult{ DeletionStatus::kFailed, ec.message() };

		// The cascade comes after the target: what it frees is only free once the referrer is gone, so
		// a failure part-way never leaves a referenced asset missing.
		for (const std::string& freed : plan.cascade)
		{
			std::filesystem::remove(GetDataRoot() / freed, ec);
			if (ec)
				return DeletionResult{ DeletionStatus::kFailed, ec.message() };
		}

		return DeletionResult{ DeletionStatus::kDeleted, {} };
	}
}
