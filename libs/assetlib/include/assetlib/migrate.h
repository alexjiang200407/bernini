#pragma once

namespace assetlib
{
	/** What happened to one file under migrateProject. */
	struct MigratedFile
	{
		enum class Outcome
		{
			kUnchanged,  // already what the current serializer writes
			kRewritten,  // read at an older schema and re-saved -- or would be, on a dry run
			kFailed      // could not be read or converted; `message` says why
		};

		std::filesystem::path path;
		Outcome               outcome;
		std::string           message;
	};

	struct MigrateReport
	{
		std::vector<MigratedFile> files;

		[[nodiscard]] size_t
		Count(MigratedFile::Outcome outcome) const noexcept;
	};

	/**
	 * Every authored container under `dataRoot` -- `.bmesh`, `.bskel`, `.banim`, `.bmaterial`,
	 * `.bsky`, `.benvl`, `.benv`; never `.bvat`, which is derived and re-baked -- read at whatever
	 * schema it carries and re-saved at the current one.
	 *
	 * A file whose bytes are already what the current serializer writes is left untouched: the
	 * containers are binaries under version control, and a rewrite that changed nothing would dirty
	 * a whole project for nothing -- so running this twice rewrites nothing the second time. A file
	 * that cannot be read is reported and skipped, never half-written; the others still go through.
	 *
	 * @param dryRun Report what would be rewritten and write nothing.
	 * @throws std::runtime_error if `dataRoot` is not a directory.
	 */
	[[nodiscard]] MigrateReport
	migrateProject(const std::filesystem::path& dataRoot, bool dryRun);
}
