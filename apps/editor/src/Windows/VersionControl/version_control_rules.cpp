#include "Windows/VersionControl/version_control_rules.h"

#include "VersionControl/contained_path.h"

#include <QFileInfo>
#include <QLocale>

namespace
{
	/** At most three named, then a count: a dialog listing forty assets says nothing. */
	QString
	Joined(QStringList names)
	{
		constexpr qsizetype c_MostToName = 3;

		if (names.size() <= c_MostToName)
		{
			return names.join(QStringLiteral(", "));
		}

		const qsizetype rest = names.size() - c_MostToName;
		names                = names.mid(0, c_MostToName);
		return QStringLiteral("%1 and %2 more").arg(names.join(QStringLiteral(", "))).arg(rest);
	}

	/** The assets a status is about, by name rather than by where the project keeps them. */
	QString
	Named(const std::vector<QString>& assets)
	{
		QStringList names;
		for (const QString& asset : assets)
		{
			names << QFileInfo(asset).fileName();
		}
		return Joined(names);
	}

	/** Each asset that would go, with something that still needs it: `X (used by Y)`. */
	QString
	NamedWithHolders(const editor::VersionControlOutcome& outcome)
	{
		QStringList pairs;
		for (qsizetype at = 0; at < std::ssize(outcome.assets); ++at)
		{
			const QString asset = QFileInfo(outcome.assets[at]).fileName();
			if (at >= std::ssize(outcome.neededBy))
			{
				pairs << asset;
				continue;
			}
			pairs << QStringLiteral("%1 (used by %2)")
						 .arg(asset, QFileInfo(outcome.neededBy[at]).fileName());
		}
		return Joined(pairs);
	}
}

namespace editor
{
	std::vector<QString>
	HeldOpenAmong(
		const std::filesystem::path& repositoryRoot,
		const std::vector<QString>&  changing,
		const QStringList&           heldOpen)
	{
		QStringList open;
		for (const QString& path : heldOpen)
		{
			if (auto relative = RelativeToRoot(repositoryRoot, path.toStdWString()))
			{
				open << QString::fromStdWString(relative->generic_wstring());
			}
		}

		std::vector<QString> held;
		for (const QString& asset : changing)
		{
			if (open.contains(asset))
			{
				held.push_back(asset);
			}
		}
		return held;
	}

	QString
	DescribeChange(ChangeKind kind)
	{
		switch (kind)
		{
		case ChangeKind::kAdded:
			return QStringLiteral("New");
		case ChangeKind::kDeleted:
			return QStringLiteral("Removed");
		case ChangeKind::kRenamed:
			return QStringLiteral("Moved");
		case ChangeKind::kModified:
			break;
		}
		return QStringLiteral("Changed");
	}

	QString
	DescribeOutcome(const VersionControlOutcome& outcome)
	{
		switch (outcome.status)
		{
		case VersionControlStatus::kDone:
			return {};

		case VersionControlStatus::kNoIdentity:
			return QStringLiteral(
				"This computer has not been set up to submit yet. Ask whoever set the project up.");

		case VersionControlStatus::kNothingToDo:
			return QStringLiteral("There is nothing to do.");

		case VersionControlStatus::kCouldNotReachShared:
			return QStringLiteral(
				"The shared project could not be reached. Check your connection and try again.");

		case VersionControlStatus::kWorkHasMovedOn:
			return QStringLiteral(
					   "Somebody else has submitted since you last got the latest (%1). "
					   "Get Latest first, then try again.")
			    .arg(Named(outcome.assets));

		case VersionControlStatus::kWouldNotFastForward:
			return QStringLiteral(
					   "You and the shared project have both changed the same assets (%1). "
					   "Somebody has to "
					   "decide which version wins, so ask whoever looks after the project.")
			    .arg(Named(outcome.assets));

		case VersionControlStatus::kAssetsInTheWay:
			return QStringLiteral(
					   "You have unsubmitted work on assets this would overwrite (%1). Submit it "
					   "or revert "
					   "it first.")
			    .arg(Named(outcome.assets));

		case VersionControlStatus::kAssetsStillInUse:
			return QStringLiteral(
					   "%1 cannot be removed, because the project would be left broken. Ask "
					   "whoever "
					   "looks after the project.")
			    .arg(NamedWithHolders(outcome));

		case VersionControlStatus::kAssetsChangedSince:
			return QStringLiteral(
					   "These assets have been changed since (%1), so this cannot be undone on its "
					   "own.")
			    .arg(Named(outcome.assets));
		}
		return {};
	}

	QString
	DescribeRevert(size_t count)
	{
		return QStringLiteral(
				   "Throw away what you have done to %1 asset(s) since your last submission? There "
				   "is "
				   "nowhere to get it back from.")
		    .arg(count);
	}

	QString
	DescribeUnavailable()
	{
		return QStringLiteral(
			"This project is not shared with anybody, so there is nothing to submit to or get "
			"from. "
			"Whoever set the project up can say why.");
	}

	QString
	DescribeHeldOpen(const std::vector<QString>& held)
	{
		return QStringLiteral(
				   "The editor still has these open (%1). Close them first, or what this does to "
				   "them "
				   "will be written straight back over.")
		    .arg(Named(held));
	}

	QString
	DefaultSubmitMessage(const std::vector<PendingChange>& chosen)
	{
		if (chosen.empty())
		{
			return {};
		}
		if (chosen.size() == 1)
		{
			return QStringLiteral("%1 %2").arg(
				DescribeChange(chosen.front().kind),
				QFileInfo(chosen.front().path).fileName());
		}
		return QStringLiteral("Updated %1 assets").arg(chosen.size());
	}

	QString
	DescribeSubmission(const Submission& submission)
	{
		// The first line only: a message made here is one line, but one made at a terminal carries a
		// whole body, and this is building a single row.
		return QStringLiteral("%1 — %2 — %3")
		    .arg(
				QLocale().toString(submission.when, QLocale::ShortFormat),
				submission.author,
				submission.message.split('\n').front().trimmed());
	}
}
