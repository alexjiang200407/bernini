#include "Revisions/GitRevisionControl.h"

#include "Revisions/git_cli.h"

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
	GitRevisionControl::GitRevisionControl(std::filesystem::path repositoryRoot) noexcept :
		m_RepositoryRoot(std::move(repositoryRoot))
	{}

	std::vector<PendingChange>
	GitRevisionControl::ListChanges() const
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
}
