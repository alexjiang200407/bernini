#include "VersionControl/reference_guard.h"

#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>

namespace
{
	namespace fs = std::filesystem;

	/**
	 * `path` as the reference graph names it: relative to the data root and `/`-separated, or nothing
	 * when it lies outside.
	 *
	 * Both sides are resolved before they are compared, and that is not tidiness: the two roots
	 * reach this from different places -- one through git, which reports a path with its symlinks
	 * already resolved, and one from the project, which does not. Compared as written, a data root
	 * behind a symlink puts every asset *outside* itself, and a guard that finds nothing is a guard
	 * that permits everything.
	 */
	std::optional<std::string>
	AssetKey(const fs::path& dataDirectory, const fs::path& path)
	{
		std::error_code ec;
		const fs::path  resolved = fs::weakly_canonical(path, ec);
		const fs::path  relative =
			(ec ? path.lexically_normal() : resolved).lexically_relative(dataDirectory);

		if (relative.empty() || relative == "." || *relative.begin() == "..")
		{
			return std::nullopt;
		}
		return relative.generic_string();
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
			for (const assetlib::AssetRef& ref : graph.ReferrersOf(key))
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
