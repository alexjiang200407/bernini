#include "blend_sets.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <assetlib/project_layout.h>
#include <core/err/util.h>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
	std::string
	BlendSetFor(const assetlib::AssetRefGraph& graph, const std::string_view animationsKey)
	{
		if (animationsKey.empty())
			return {};

		try
		{
			std::string key = assetlib::blendSetKeyFor(animationsKey);
			return graph.Contains(key) ? std::move(key) : std::string();
		}
		catch (const std::exception&)
		{
			return {};
		}
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
	CreateEmptyBlendSet(const assetlib::AssetStore& store, const std::string_view animationsKey)
	{
		const std::string key = assetlib::blendSetKeyFor(animationsKey);

		if (store.GetFiles().Stat(key).has_value())
		{
			core::throw_runtime_error(
				"'{}' already exists; edit it rather than starting over",
				key);
		}

		auto set       = assetlib::BlendSet();
		set.animations = std::string(animationsKey);
		store.Save(set, key);
		return key;
	}

	assetlib::BlendSet
	LoadBlendSet(const assetlib::AssetStore& store, const std::string_view key)
	{
		return store.Load<assetlib::BlendSet>(key);
	}

	void
	SaveBlendSet(
		const assetlib::AssetStore& store,
		const std::string_view      key,
		const assetlib::BlendSet&   set)
	{
		store.Save(set, key);
	}
}
