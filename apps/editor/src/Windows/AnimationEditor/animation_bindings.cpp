#include "animation_bindings.h"

#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh_io.h>

namespace editor
{
	AnimationBindings
	ResolveAnimationBindings(const std::filesystem::path& dataRoot, std::string_view meshRelPath)
	{
		auto bindings     = AnimationBindings();
		bindings.skeleton = assetlib::loadMeshRefs(dataRoot / meshRelPath).skeleton;
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
