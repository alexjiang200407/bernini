#include "animation_bindings.h"

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <filesystem>
#include <string_view>

namespace editor
{
	AnimationBindings
	ResolveAnimationBindings(const std::filesystem::path& dataRoot, std::string_view skeleton)
	{
		auto bindings     = AnimationBindings();
		bindings.skeleton = std::string(skeleton);
		if (bindings.skeleton.empty())
			return bindings;

		const auto graph = assetlib::AssetRefGraph::Scan(assetlib::AssetStore(dataRoot));
		for (const assetlib::AssetRef& ref : graph.ReferrersOf(bindings.skeleton))
			if (ref.kind == assetlib::RefKind::kClipSkeleton)
				bindings.animations.push_back(ref.referrer);

		std::sort(bindings.animations.begin(), bindings.animations.end());
		return bindings;
	}
}
