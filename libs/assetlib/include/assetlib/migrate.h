#pragma once

namespace assetlib
{
	/** What happened to one file under migrateProject. */
	struct MigratedFile
	{
		enum class Outcome
		{
			kUnchanged,  // already what the current serializer writes
			kRewritten,  // not byte-identical to the current form; re-saved -- or would be, on a dry run
			kFailed  // could not be read or converted; `message` says why
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
	 * Every container under `dataRoot` re-saved at what the project's current state says it
	 * should hold; never `.bvat`, which is derived and re-baked. Geometry (`.bmesh`, `.bskel`,
	 * `.banim`) reads through the regeneration seam: a stale group re-cooks from its copied
	 * source onto disk, and a binding-only document edit reaches the file without one -- a
	 * missing source or a binding naming a vanished submesh is that file's failure. The rest
	 * (`.bmaterial`, `.bsky`, `.benvl`, `.benv`) is read and re-saved at the current form.
	 *
	 * A file whose bytes are already what the current writer produces is left untouched: the
	 * containers are files under version control, and a rewrite that changed nothing would dirty
	 * a whole project for nothing -- so running this twice rewrites nothing the second time. A
	 * file that cannot be read is reported and skipped, never half-written; the others still go
	 * through.
	 *
	 * @param dryRun Report what would be rewritten and write nothing.
	 * @throws std::runtime_error if `dataRoot` is not a directory.
	 */
	[[nodiscard]] MigrateReport
	migrateProject(const std::filesystem::path& dataRoot, bool dryRun);
}
