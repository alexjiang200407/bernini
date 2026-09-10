#include "blend_sets.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <core/err/util.h>
#include <filesystem>
#include <string>
#include <string_view>
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
}
