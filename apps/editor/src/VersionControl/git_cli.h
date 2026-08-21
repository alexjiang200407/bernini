#pragma once

#include <QByteArray>
#include <QStringList>

namespace editor
{
	/** How long RunGit waits for a `git` that has stopped making progress. */
	static constexpr auto c_GitTimeoutMs = 120'000;

	/** One `git` invocation's exit status and its two streams, decoded by nobody. */
	struct GitCommandResult
	{
		// nullopt when git could not be found, could not be started, was killed by a signal, or did
		// not exit in time.
		std::optional<int> exitCode;

		QByteArray out;
		QByteArray err;

		[[nodiscard]] bool
		Succeeded() const noexcept
		{
			return exitCode == 0;
		}
	};

	/**
	 * Runs `git` in `workingDirectory` and blocks until it exits.
	 *
	 * `paths` are appended after a `--` separator, which is what makes an asset named `-o` a path
	 * rather than an option. Pass them here and not inside `arguments`: this is the only place that
	 * guarantee holds, and every path should have come through RepositoryRelativePath first.
	 *
	 * QProcess buffers both channels while it waits, so output larger than a pipe buffer cannot
	 * deadlock the child. No shell is involved, so nothing is quoted, globbed or expanded.
	 *
	 * The child gets `GIT_TERMINAL_PROMPT=0`, so a repository whose credentials are missing fails
	 * rather than blocking forever on a prompt that has no window to appear in.
	 *
	 * `timeoutMs` bounds the whole call, starting included, rather than each wait separately.
	 *
	 * Safe to call from a worker thread: the QProcess is created, run and destroyed inside the call,
	 * on whichever thread makes it.
	 */
	[[nodiscard]] GitCommandResult
	RunGit(
		const std::filesystem::path& workingDirectory,
		const QStringList&           arguments,
		const QStringList&           paths     = {},
		int                          timeoutMs = c_GitTimeoutMs);

	/**
	 * The root of the repository containing `startingPoint`, or nullopt when there is none.
	 *
	 * Asked of git rather than by walking up looking for `.git`, so a linked worktree, a `.git` file
	 * and a repository git declines to trust all answer the way git itself would.
	 */
	[[nodiscard]] std::optional<std::filesystem::path>
	FindRepositoryRoot(const std::filesystem::path& startingPoint);

	/**
	 * `path` as a `/`-separated path relative to `repositoryRoot`, or nullopt when it escapes it.
	 *
	 * The fence every path crosses before it reaches RunGit: a relative path is resolved against the
	 * root first, and anything that normalizes to outside it -- `..`, an absolute path elsewhere, a
	 * Windows drive-relative `D:foo` -- comes back nullopt rather than reaching git.
	 *
	 * Symlinks are resolved on both sides before the comparison, so a path that reaches outside the
	 * repository through one is refused too, and a repository reached through one still matches. A
	 * file that does not exist is still namable: only the part of the path that does exist resolves.
	 */
	[[nodiscard]] std::optional<QString>
	RepositoryRelativePath(
		const std::filesystem::path& repositoryRoot,
		const std::filesystem::path& path);
}
