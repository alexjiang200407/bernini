#include <assetlib/asset_refs.h>

#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_MeshExtension      = ".bmesh";
		constexpr std::string_view c_MaterialExtension  = ".bmaterial";
		constexpr std::string_view c_TextureExtension   = ".ktx2";
		constexpr std::string_view c_SkeletonExtension  = ".bskel";
		constexpr std::string_view c_AnimationExtension = ".banim";

		/**
		 * The one form every path in the graph is keyed and stored in, so that the two sides of a reference
		 * -- one written by a bake, one clicked in a file browser -- meet. Identity in this project is the
		 * data-root-relative path, and `Textures/x.ktx2` and `./Meshes/../Textures/x.ktx2` are one asset.
		 */
		std::string
		normalizeRef(std::string_view path)
		{
			return std::filesystem::path(path).lexically_normal().generic_string();
		}

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

		/** Whether `path` lies beneath `directory`. Both normalized, and neither is inside itself. */
		bool
		isUnder(std::string_view path, std::string_view directory)
		{
			return path.size() > directory.size() + 1 && path.starts_with(directory) &&
			       path[directory.size()] == '/';
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

		/** Every material a `.bmesh` names, in `mesh.materials` order, and the skeleton it skins to. */
		void
		collectMeshEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			MeshRefs refs;
			try
			{
				refs = loadMeshRefs(file);
			}
			catch (const std::exception& e)
			{
				// Fatal, as it is for the prune: a mesh we cannot read is a mesh whose materials we cannot
				// see, and we would then delete one of them out from under it.
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the mesh '" + file.string() +
					"', so the assets it references cannot be known: " + e.what());
			}

			for (const std::string& material : refs.materials)
				addEdge(edges, referrer, material, RefKind::kSubmeshMaterial);

			addEdge(edges, referrer, refs.skeleton, RefKind::kMeshSkeleton);
		}

		/** The skeleton a `.banim`'s clips were resampled against. */
		void
		collectAnimationEdges(
			std::vector<AssetRef>&       edges,
			const std::filesystem::path& file,
			const std::string&           referrer)
		{
			std::string skeleton;
			try
			{
				skeleton = loadAnimationSkeletonPath(file);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(
					"assetlib::AssetRefGraph: cannot read the clip set '" + file.string() +
					"', so the skeleton it references cannot be known: " + e.what());
			}

			addEdge(edges, referrer, skeleton, RefKind::kClipSkeleton);
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
		if (ext == c_SkeletonExtension)
			return AssetType::kSkeleton;
		if (ext == c_AnimationExtension)
			return AssetType::kAnimation;

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
			else if (kind == c_AnimationExtension)
			{
				collectAnimationEdges(edges, file, referrer);
				++graph.clipSetsScanned;
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

	DeletionPlan
	planDeletion(const AssetRefGraph& graph, std::string_view target)
	{
		auto plan   = DeletionPlan();
		plan.target = normalizeRef(target);

		// The data root is not a thing inside the data root, and neither is anything above it.
		if (plan.target.empty() || plan.target == "." || plan.target == ".." ||
		    plan.target.starts_with("../"))
			throw std::runtime_error(
				"assetlib::planDeletion: '" + std::string(target) +
				"' does not name something inside the data root");

		// A directory is not an asset, and has no kind: that is what nullopt says.
		if (std::filesystem::is_directory(graph.DataRoot() / plan.target))
		{
			plan.contents = filesUnder(graph.DataRoot(), plan.target);
			plan.blockers = graph.ReferrersInto(plan.target);

			return plan;
		}

		plan.assetType = assetTypeFromExtension(plan.target);
		if (!plan.assetType)
			throw std::runtime_error(
				"assetlib::planDeletion: '" + plan.target +
				"' is not an asset this project stores anything about");

		const std::span<const AssetRef> referrers = graph.ReferrersOf(plan.target);
		plan.blockers.assign(referrers.begin(), referrers.end());

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

		return DeletionResult{ DeletionStatus::kDeleted, {} };
	}
}
