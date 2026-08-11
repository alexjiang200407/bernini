#include <assetlib/asset_refs.h>

#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>

#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_MeshExtension        = ".bmesh";
		constexpr std::string_view c_MaterialExtension    = ".bmaterial";
		constexpr std::string_view c_TextureExtension     = ".ktx2";
		constexpr std::string_view c_EnvironmentExtension = ".benv";
		constexpr std::string_view c_SkyExtension         = ".bsky";
		constexpr std::string_view c_EnvLightingExtension = ".benvl";

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

		/** Every file beneath `directory`, relative to the data root -- all of which it takes with it. */
		std::vector<std::string>
		filesUnder(const std::filesystem::path& dataRoot, const std::string& directory)
		{
			auto out = std::vector<std::string>();

			for (const auto& entry :
			     std::filesystem::recursive_directory_iterator(dataRoot / directory))
			{
				if (!entry.is_regular_file())
					continue;

				out.push_back(normalizeRef(
					std::filesystem::relative(entry.path(), dataRoot).generic_string()));
			}

			std::ranges::sort(out);
			return out;
		}

		/** Every material a `.bmesh` names, in `mesh.materials` order. */
		void
		collectMeshEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			std::vector<std::string> materials;
			try
			{
				materials = loadMaterialPaths(file);
			}
			catch (const std::exception& e)
			{
				// Fatal, as it is for the prune: a mesh we cannot read is a mesh whose materials we cannot
				// see, and we would then delete one of them out from under it.
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the mesh '" + file.string() +
					"', so the materials it references cannot be known: " + e.what());
			}

			for (const std::string& material : materials)
				addEdge(edges, referrer, material, RefKind::kSubmeshMaterial);
		}

		/** The baked triplet a `.bmaterial` names, and the sources its channels route from. */
		void
		collectMaterialEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			auto material = BMaterial();
			try
			{
				material = loadMaterial(file);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the material '" + file.string() +
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
					"assetlib::AssetRefGraph: the material '" + file.string() +
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
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			try
			{
				addRouteEdges(edges, referrer, loadSky(file).sky);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the sky '" + file.string() +
					"', so the textures it references cannot be known: " + e.what());
			}
		}

		/** Both halves of a `.benvl`: each names a source and the map convolved from it. */
		void
		collectEnvLightingEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			try
			{
				const BEnvLighting lighting = loadEnvLighting(file);
				addRouteEdges(edges, referrer, lighting.prefilter);
				addRouteEdges(edges, referrer, lighting.irradiance);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the environment lighting '" +
					file.string() +
					"', so the textures it references cannot be known: " + e.what());
			}
		}

		/** The pair a `.benv` composes. It holds no pixels, so these are its only edges. */
		void
		collectEnvironmentEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			try
			{
				const BEnv env = loadEnv(file);
				addEdge(edges, referrer, env.sky, RefKind::kEnvironmentPart);
				addEdge(edges, referrer, env.lighting, RefKind::kEnvironmentPart);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the environment '" + file.string() +
					"', so the assets it composes cannot be known: " + e.what());
			}
		}
	}

	std::optional<AssetType>
	assetTypeFromExtension(const std::filesystem::path& path)
	{
		const std::string ext = lowerExtension(path);

		if (ext == c_MeshExtension)
			return AssetType::kMesh;
		if (ext == c_MaterialExtension)
			return AssetType::kMaterial;
		if (ext == c_TextureExtension)
			return AssetType::kTexture;
		if (ext == c_EnvironmentExtension)
			return AssetType::kEnvironment;
		if (ext == c_SkyExtension)
			return AssetType::kSky;
		if (ext == c_EnvLightingExtension)
			return AssetType::kEnvLighting;

		return std::nullopt;
	}

	AssetRefGraph
	AssetRefGraph::Scan(const AssetRefScanDesc& desc)
	{
		if (!std::filesystem::is_directory(desc.dataRoot))
			throw std::runtime_error(
				"assetlib::AssetRefGraph: the data root '" + desc.dataRoot.string() +
				"' is not a directory");

		auto graph       = AssetRefGraph();
		graph.m_DataRoot = desc.dataRoot;

		auto edges = std::vector<AssetRef>();

		for (const auto& entry : std::filesystem::recursive_directory_iterator(desc.dataRoot))
		{
			if (!entry.is_regular_file())
				continue;

			const std::filesystem::path& file = entry.path();
			const std::string            kind = lowerExtension(file);

			const std::string referrer =
				normalizeRef(std::filesystem::relative(file, desc.dataRoot).generic_string());

			if (kind == c_MeshExtension)
			{
				collectMeshEdges(edges, file, referrer);
				++graph.meshesScanned;
			}
			else if (kind == c_MaterialExtension)
			{
				collectMaterialEdges(edges, file, referrer);
				++graph.materialsScanned;
			}
			else if (kind == c_SkyExtension)
			{
				collectSkyEdges(edges, file, referrer);
				++graph.environmentsScanned;
			}
			else if (kind == c_EnvLightingExtension)
			{
				collectEnvLightingEdges(edges, file, referrer);
				++graph.environmentsScanned;
			}
			else if (kind == c_EnvironmentExtension)
			{
				collectEnvironmentEdges(edges, file, referrer);
				++graph.environmentsScanned;
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

			if (!std::filesystem::exists(desc.dataRoot / target))
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

			auto cascade = std::vector<std::string>();

			for (bool grew = true; grew;)
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

					// A target that is not on disk is a broken edge, not something to delete.
					if (!std::filesystem::exists(graph.DataRoot() / edge.target))
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
	planDeletion(const AssetRefGraph& graph, std::string_view target, DeletionMode mode)
	{
		auto plan   = DeletionPlan();
		plan.target = normalizeRef(target);

		requireInsideDataRoot("assetlib::planDeletion", plan.target);

		// A directory is not an asset, and has no kind: that is what nullopt says.
		if (std::filesystem::is_directory(graph.DataRoot() / plan.target))
		{
			plan.contents = filesUnder(graph.DataRoot(), plan.target);
			plan.blockers = graph.ReferrersInto(plan.target);
		}
		else
		{
			plan.assetType = assetTypeFromExtension(plan.target);
			if (!plan.assetType)
				throw std::runtime_error(
					"assetlib::planDeletion: '" + plan.target +
					"' is not an asset this project stores anything about");

			const std::span<const AssetRef> referrers = graph.ReferrersOf(plan.target);
			plan.blockers.assign(referrers.begin(), referrers.end());
		}

		// A blocked deletion frees nothing, so there is no cascade to compute for one.
		if (mode == DeletionMode::kCascade && plan.Allowed())
			plan.cascade = cascadeOf(graph, plan);

		return plan;
	}

	DeletionResult
	deleteAsset(const DeletionPlan& plan, const AssetRefScanDesc& desc)
	{
		if (!plan.Allowed())
			return DeletionResult{ DeletionStatus::kRefused, {} };

		const std::filesystem::path path = desc.dataRoot / plan.target;

		std::error_code ec;
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
			std::filesystem::remove(desc.dataRoot / freed, ec);
			if (ec)
				return DeletionResult{ DeletionStatus::kFailed, ec.message() };
		}

		return DeletionResult{ DeletionStatus::kDeleted, {} };
	}
}
