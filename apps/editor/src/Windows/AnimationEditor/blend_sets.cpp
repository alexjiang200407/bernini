#include "blend_sets.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <assetlib/project_layout.h>
#include <core/err/util.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
	std::vector<std::string>
	ResolveBlendSets(const assetlib::AssetRefGraph& graph, const std::string_view animationsKey)
	{
		auto sets = std::vector<std::string>();
		if (animationsKey.empty())
			return sets;

		for (const assetlib::AssetRef& ref : graph.ReferrersOf(animationsKey))
			if (ref.kind == assetlib::RefKind::kBlendClips)
				sets.push_back(ref.referrer);

		std::ranges::sort(sets);
		return sets;
	}

	std::vector<std::string>
	ResolveBlendSetMeshes(const assetlib::AssetRefGraph& graph, const std::string_view blendSetKey)
	{
		auto meshes = std::vector<std::string>();

		for (const assetlib::AssetRef& clips : graph.ReferencesOf(blendSetKey))
		{
			if (clips.kind != assetlib::RefKind::kBlendClips)
				continue;

			for (const assetlib::AssetRef& rig : graph.ReferencesOf(clips.target))
			{
				if (rig.kind != assetlib::RefKind::kClipSkeleton)
					continue;

				for (const assetlib::AssetRef& mesh : graph.ReferrersOf(rig.target))
					if (mesh.kind == assetlib::RefKind::kMeshSkeleton)
						meshes.push_back(mesh.referrer);
			}
		}

		std::ranges::sort(meshes);
		return meshes;
	}

	std::vector<std::string>
	ClipSetsWithoutBlendSet(const assetlib::AssetRefGraph& graph)
	{
		auto clipSets = std::vector<std::string>();

		for (std::string& file : graph.GetFilesUnder(assetlib::c_AnimationsDirectoryName))
			if (assetlib::assetTypeFromExtension(file) == assetlib::AssetType::kAnimation &&
			    !graph.Contains(assetlib::blendSetKeyFor(file)))
				clipSets.push_back(std::move(file));

		return clipSets;
	}

	std::string
	CreateEmptyBlendSet(const std::filesystem::path& dataRoot, const std::string_view animationsKey)
	{
		const std::string key = assetlib::blendSetKeyFor(animationsKey);

		const assetlib::AssetStore store(dataRoot);
		core::throw_runtime_error_if(
			store.GetFiles().Stat(key).has_value(),
			"'{}' already exists; edit it rather than starting over",
			key);

		auto set       = assetlib::BlendSet();
		set.animations = std::string(animationsKey);
		store.Save(set, key);
		return key;
	}

	assetlib::BlendSet
	LoadBlendSet(const std::filesystem::path& dataRoot, const std::string_view key)
	{
		const assetlib::AssetStore store(dataRoot);
		return store.Load<assetlib::BlendSet>(key);
	}

	void
	SaveBlendSet(
		const std::filesystem::path& dataRoot,
		const std::string_view       key,
		const assetlib::BlendSet&    set)
	{
		const assetlib::AssetStore store(dataRoot);
		store.Save(set, key);
	}
}
