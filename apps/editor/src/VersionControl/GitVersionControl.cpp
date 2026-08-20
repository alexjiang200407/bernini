#include "VersionControl/GitVersionControl.h"

#include "VersionControl/git_cli.h"

#include <core/err/util.h>

namespace
{
	/**
	 * What follows the first `fieldCount` space-separated fields of `record`, empty when there are
	 * fewer than that many.
	 *
	 * Porcelain v2 puts a fixed number of fields ahead of the path and nothing behind it, so the path
	 * is whatever is left -- which is the only way one containing a space survives.
	 */
	QByteArray
	AfterFields(const QByteArray& record, int fieldCount)
	{
		int at = 0;
		for (int field = 0; field < fieldCount; ++field)
		{
			at = record.indexOf(' ', at);
			if (at < 0)
			{
				return {};
			}
			++at;
		}
		return record.mid(at);
	}

	/** The `-z` output of a command that lists paths, one QString per path. */
	std::vector<QString>
	PathsFrom(const editor::GitCommandResult& result)
	{
		std::vector<QString> paths;
		for (const QByteArray& path : result.out.split('\0'))
		{
			if (!path.isEmpty())
			{
				paths.push_back(QString::fromUtf8(path));
			}
		}
		return paths;
	}

	/** Whether `key` is set to something, in any of the files git would read it from. */
	bool
	IsConfigured(const std::filesystem::path& root, const QString& key)
	{
		const auto result =
			editor::RunGit(root, { QStringLiteral("config"), QStringLiteral("--get"), key });
		return result.Succeeded() && !result.out.trimmed().isEmpty();
	}

	/** Every asset in `assets` fenced to the repository, or nothing when any of them escapes it. */
	std::optional<QStringList>
	FencedPaths(const std::filesystem::path& root, const std::vector<QString>& assets)
	{
		QStringList fenced;
		for (const QString& asset : assets)
		{
			const auto relative = editor::RepositoryRelativePath(root, asset.toStdWString());
			if (!relative.has_value())
			{
				return std::nullopt;
			}
			fenced << *relative;
		}
		return fenced;
	}

	/**
	 * The kind an ordinary entry's two-letter state means, or nothing when it means no net change.
	 *
	 * The user compares what is on disk now against what was last submitted, and has no staging area
	 * in between -- Submit records and publishes together. So the worktree letter is read first,
	 * because it says whether the asset is there at all: an asset staged and then deleted from disk
	 * was never submitted and is not there now, which is nothing to report rather than an addition.
	 */
	std::optional<editor::ChangeKind>
	KindOfState(const QByteArray& state)
	{
		if (state.size() < 2)
		{
			return std::nullopt;
		}

		const char staged   = state.at(0);
		const char worktree = state.at(1);

		if (worktree == 'D')
		{
			if (staged == 'A')
			{
				return std::nullopt;
			}
			return editor::ChangeKind::kDeleted;
		}
		if (staged == 'A')
		{
			return editor::ChangeKind::kAdded;
		}
		if (staged == 'D')
		{
			return editor::ChangeKind::kDeleted;
		}
		return editor::ChangeKind::kModified;
	}
}

namespace editor
{
	GitVersionControl::GitVersionControl(std::filesystem::path repositoryRoot) noexcept :
		m_RepositoryRoot(std::move(repositoryRoot))
	{}

	bool
	GitVersionControl::HasIdentity() const
	{
		return IsConfigured(m_RepositoryRoot, QStringLiteral("user.name")) &&
		       IsConfigured(m_RepositoryRoot, QStringLiteral("user.email"));
	}

	std::optional<int>
	GitVersionControl::CountSubmissions(const QString& range) const
	{
		const auto result = RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("rev-list"), QStringLiteral("--count"), range });

		if (!result.Succeeded())
		{
			return std::nullopt;
		}
		// Anything but a number is a count nobody can act on, and a silent 0 would read as "level with
		// the shared project" and wave every check below straight through.
		bool       parsed = false;
		const auto count  = QString::fromUtf8(result.out).trimmed().toInt(&parsed);
		return parsed ? std::optional<int>(count) : std::nullopt;
	}

	std::vector<QString>
	GitVersionControl::ChangedBetween(const QString& from, const QString& to) const
	{
		const auto result = RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("diff"),
		      QStringLiteral("--name-only"),
		      QStringLiteral("-z"),
		      from,
		      to });

		core::throw_runtime_error_if(
			!result.Succeeded(),
			"the project could not be compared with the shared one");

		return PathsFrom(result);
	}

	std::vector<PendingChange>
	GitVersionControl::ListChanges() const
	{
		// -z rather than the default: it is what stops git C-quoting a path with a space or a
		// non-ASCII byte in it, so the bytes arrive as the filesystem holds them.
		const auto result = RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("status"),
		      QStringLiteral("--porcelain=v2"),
		      QStringLiteral("-z"),
		      QStringLiteral("--untracked-files=all") });

		core::throw_runtime_error_if(
			!result.Succeeded(),
			"the project's pending changes could not be read from '{}'",
			m_RepositoryRoot.string());

		const QList<QByteArray> records = result.out.split('\0');

		std::vector<PendingChange> changes;
		changes.reserve(records.size());

		for (qsizetype at = 0; at < records.size(); ++at)
		{
			const QByteArray& record = records[at];
			if (record.isEmpty())
			{
				continue;
			}

			PendingChange change;
			switch (record.front())
			{
			case '1':
			{
				const auto kind = KindOfState(record.mid(2, 2));
				if (!kind.has_value())
				{
					continue;
				}
				change.kind = *kind;
				change.path = QString::fromUtf8(AfterFields(record, 8));
				break;
			}

			case '2':
				// A rename entry is two records: the entry, then the path it came from.
				change.kind = ChangeKind::kRenamed;
				change.path = QString::fromUtf8(AfterFields(record, 9));
				if (++at < records.size())
				{
					change.renamedFrom = QString::fromUtf8(records[at]);
				}
				break;

			case 'u':
				// Nothing here produces a conflicted asset -- Get Latest fast-forwards or refuses --
				// but one arrived at through a terminal is still a change the user has made.
				change.kind = ChangeKind::kModified;
				change.path = QString::fromUtf8(AfterFields(record, 10));
				break;

			case '?':
				// An asset git has never seen is one the user added, whatever git calls it.
				change.kind = ChangeKind::kAdded;
				change.path = QString::fromUtf8(AfterFields(record, 1));
				break;

			default:
				continue;
			}

			if (!change.path.isEmpty())
			{
				changes.push_back(std::move(change));
			}
		}

		std::ranges::sort(changes, {}, &PendingChange::path);
		return changes;
	}

	VersionControlOutcome
	GitVersionControl::Submit(const std::vector<QString>& assets, const QString& message)
	{
		const auto fenced = FencedPaths(m_RepositoryRoot, assets);
		core::throw_runtime_error_if(
			!fenced.has_value(),
			"an asset outside the project was offered for submission");

		if (fenced->isEmpty())
		{
			return { .status = VersionControlStatus::kNothingToDo };
		}

		if (!HasIdentity())
		{
			return { .status = VersionControlStatus::kNoIdentity };
		}

		if (!RunGit(m_RepositoryRoot, { QStringLiteral("fetch"), QStringLiteral("--quiet") })
		         .Succeeded())
		{
			return { .status = VersionControlStatus::kCouldNotReachShared };
		}

		// Before anything is recorded, not after: a recorded change that cannot then be published is
		// exactly the "looks saved and is not" state Submit exists as one action to avoid.
		const auto behind = CountSubmissions(QStringLiteral("HEAD..@{upstream}"));
		if (!behind.has_value())
		{
			return { .status = VersionControlStatus::kCouldNotReachShared };
		}
		if (*behind > 0)
		{
			return {
				.status = VersionControlStatus::kWorkHasMovedOn,
				.assets = ChangedBetween(QStringLiteral("HEAD"), QStringLiteral("@{upstream}")),
			};
		}

		// Unchecked, a staging failure would read as a clean index two lines down and be reported as
		// nothing to submit.
		const auto staged =
			RunGit(m_RepositoryRoot, { QStringLiteral("add"), QStringLiteral("-A") }, *fenced);
		core::throw_runtime_error_if(
			!staged.Succeeded(),
			"the project's changes could not be prepared for submission");

		// --quiet exits 1 when there is something staged, which is the interesting answer.
		if (RunGit(
				m_RepositoryRoot,
				{ QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--quiet") })
		        .Succeeded())
		{
			return { .status = VersionControlStatus::kNothingToDo };
		}

		const auto committed = RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("-m"), message });
		core::throw_runtime_error_if(
			!committed.Succeeded(),
			"the project's changes could not be recorded");

		if (!RunGit(m_RepositoryRoot, { QStringLiteral("push"), QStringLiteral("--quiet") })
		         .Succeeded())
		{
			// Someone published between the check above and here. Unrecord it rather than leave work
			// that looks submitted; the assets stay staged, so nothing the user did is lost.
			const auto unrecorded = RunGit(
				m_RepositoryRoot,
				{ QStringLiteral("reset"), QStringLiteral("--soft"), QStringLiteral("HEAD~1") });

			// Recorded, unpublishable, and now un-unrecordable: no refusal can describe this
			// truthfully, and the state needs someone who knows what a repository is.
			core::throw_runtime_error_if(
				!unrecorded.Succeeded(),
				"the project's changes were recorded but could neither be published nor undone");

			if (!RunGit(m_RepositoryRoot, { QStringLiteral("fetch"), QStringLiteral("--quiet") })
			         .Succeeded())
			{
				return { .status = VersionControlStatus::kWorkHasMovedOn };
			}
			return {
				.status = VersionControlStatus::kWorkHasMovedOn,
				.assets = ChangedBetween(QStringLiteral("HEAD"), QStringLiteral("@{upstream}")),
			};
		}
		return {};
	}

	VersionControlOutcome
	GitVersionControl::GetLatest()
	{
		if (!RunGit(m_RepositoryRoot, { QStringLiteral("fetch"), QStringLiteral("--quiet") })
		         .Succeeded())
		{
			return { .status = VersionControlStatus::kCouldNotReachShared };
		}

		// Counted in submissions rather than in files that differ: `git diff A..B` is a two-point
		// comparison, so a project purely behind the shared one differs from it in both directions and
		// would read as having work of its own.
		const auto behind = CountSubmissions(QStringLiteral("HEAD..@{upstream}"));
		const auto ahead  = CountSubmissions(QStringLiteral("@{upstream}..HEAD"));
		if (!behind.has_value() || !ahead.has_value())
		{
			return { .status = VersionControlStatus::kCouldNotReachShared };
		}
		if (*behind == 0)
		{
			return { .status = VersionControlStatus::kNothingToDo };
		}

		auto incoming = ChangedBetween(QStringLiteral("HEAD"), QStringLiteral("@{upstream}"));
		if (*ahead > 0)
		{
			return { .status = VersionControlStatus::kWouldNotFastForward,
				     .assets = std::move(incoming) };
		}

		// A fast-forward still overwrites an asset changed here but never submitted, and git would
		// stop halfway through. Name them instead, and touch nothing.
		std::vector<QString> inTheWay;
		for (const PendingChange& change : ListChanges())
		{
			if (std::ranges::find(incoming, change.path) != incoming.end())
			{
				inTheWay.push_back(change.path);
			}
		}
		if (!inTheWay.empty())
		{
			return { .status = VersionControlStatus::kAssetsInTheWay,
				     .assets = std::move(inTheWay) };
		}

		const auto merged = RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("merge"),
		      QStringLiteral("--ff-only"),
		      QStringLiteral("--quiet"),
		      QStringLiteral("@{upstream}") });
		core::throw_runtime_error_if(
			!merged.Succeeded(),
			"the shared project's work could not be brought in");

		return {};
	}

	VersionControlOutcome
	GitVersionControl::Revert(const std::vector<QString>& assets)
	{
		const auto fenced = FencedPaths(m_RepositoryRoot, assets);
		core::throw_runtime_error_if(
			!fenced.has_value(),
			"an asset outside the project was offered for reverting");

		if (fenced->isEmpty())
		{
			return { .status = VersionControlStatus::kNothingToDo };
		}

		// Split by whether the last submission has the asset at all: one half has something to go
		// back to, the other only exists here and so goes away entirely.
		const auto submitted = PathsFrom(RunGit(
			m_RepositoryRoot,
			{ QStringLiteral("ls-tree"),
		      QStringLiteral("HEAD"),
		      QStringLiteral("-z"),
		      QStringLiteral("--name-only"),
		      QStringLiteral("--full-tree") },
			*fenced));

		QStringList restore;
		QStringList remove;
		for (const QString& asset : *fenced)
		{
			(std::ranges::find(submitted, asset) != submitted.end() ? restore : remove) << asset;
		}

		if (!restore.isEmpty())
		{
			const auto checkedOut = RunGit(
				m_RepositoryRoot,
				{ QStringLiteral("checkout"), QStringLiteral("HEAD") },
				restore);
			core::throw_runtime_error_if(
				!checkedOut.Succeeded(),
				"an asset could not be put back as it was");
		}
		if (!remove.isEmpty())
		{
			const auto removed = RunGit(
				m_RepositoryRoot,
				{ QStringLiteral("clean"),
			      QStringLiteral("--quiet"),
			      QStringLiteral("-f"),
			      QStringLiteral("-d") },
				remove);
			core::throw_runtime_error_if(
				!removed.Succeeded(),
				"an asset that was never submitted could not be removed");
		}
		return {};
	}
}
