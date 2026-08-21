#include "VersionControl/reference_guard.h"

#include "VersionControl/contained_path.h"

#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>

namespace
{
	namespace fs = std::filesystem;

	/** `path` as the reference graph names it: relative to the data root and `/`-separated. */
	std::optional<std::string>
	AssetKey(const fs::path& dataDirectory, const fs::path& path)
	{
		const auto relative = editor::RelativeToRoot(dataDirectory, path);
		return relative.has_value() ? std::optional(relative->generic_string()) : std::nullopt;
	}

}

namespace editor
{
	std::vector<AssetStillInUse>
	FindAssetsStillInUse(const fs::path& dataDirectory, const std::vector<fs::path>& deleted)
	{
		std::error_code ec;
		const fs::path  resolvedRoot = fs::weakly_canonical(dataDirectory, ec);
		const fs::path  root         = ec ? dataDirectory.lexically_normal() : resolvedRoot;

		std::unordered_set<std::string> keys;
		keys.reserve(deleted.size());
		for (const fs::path& path : deleted)
		{
			if (auto key = AssetKey(root, path))
			{
				keys.insert(*std::move(key));
			}
		}

		// Nothing under the data root is going, so nothing can be left pointing at a hole -- and the
		// scan below is a walk of the whole project, not worth doing to answer that.
		if (keys.empty())
		{
			return {};
		}

		const auto graph = assetlib::AssetRefGraph::Scan(assetlib::AssetStore(dataDirectory));

		std::vector<AssetStillInUse> inUse;
		for (const std::string& key : keys)
		{
			// Not an asset kind this project stores anything about -- a `.gitignore`, a project file --
			// so nothing can reference it, and planDeletion would throw rather than say so.
			if (!assetlib::assetTypeFromExtension(key).has_value())
			{
				continue;
			}

			// planDeletion rather than ReferrersOf: what does and does not hold an asset back is its
			// policy, and asking the graph directly re-derives it -- badly. A `.bvat` names its inputs
			// but never blocks them, because a bake that outlived them is stale by definition, and that
			// rule lives in there.
			for (const assetlib::AssetRef& ref : assetlib::planDeletion(graph, key).blockers)
			{
				if (!keys.contains(ref.referrer))
				{
					inUse.push_back(
						{
							.asset    = root / key,
							.neededBy = root / ref.referrer,
						});
				}
			}
		}
		return inUse;
	}
}
