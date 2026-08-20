#pragma once

namespace assetlib
{
	/** What happened to one file under rebakePosedBounds. */
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

	/**
	 * Every `.banim` under `dataRoot` gets the posed culling boxes an import writes
	 * (bakePosedBounds), measured against every `.bmesh` that names its skeleton -- the retrofit
	 * for a project whose rigs were imported before the boxes existed.
	 *
	 * A clip set whose boxes already match its meshes' content is left untouched, so running this
	 * twice rewrites nothing the second time. A file that cannot be read is reported and skipped,
	 * never half-written; the others still go through.
	 *
	 * @param dryRun Report what would be rewritten and write nothing. Cheap: deciding costs a
	 *        signature per mesh, and only a real rebake pays the walk.
	 * @throws std::runtime_error if `dataRoot` is not a directory.
	 */
	[[nodiscard]] RebakeBoundsReport
	rebakePosedBounds(const std::filesystem::path& dataRoot, bool dryRun);
}
