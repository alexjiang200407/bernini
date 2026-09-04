#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
namespace assetlib
{
	/** What happened to one file under RebakePosedBounds. */
	struct RebakedFile
	{
		enum class Outcome
		{
			kCurrent,   // its boxes already match every paired mesh's content
			kRebaked,   // measured and re-saved -- or would be, on a dry run
			kOrphaned,  // a clip set no mesh under the root skins to, so there is nothing to bound
			kFailed     // could not be read, or a file it needed could not; `message` says why
		};

		std::filesystem::path path;
		Outcome               outcome;
		std::string           message;
	};

	struct RebakeBoundsReport
	{
		std::vector<RebakedFile> files;

		[[nodiscard]] size_t
		Count(RebakedFile::Outcome outcome) const noexcept;
	};
}
