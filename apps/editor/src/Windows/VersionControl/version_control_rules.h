#pragma once

#include "VersionControl/IVersionControl.h"

namespace editor
{
	/**
	 * Which of `changing` an editor window still holds open.
	 *
	 * ADR-10's other half. A window that has an asset open will write it back on its next Save, so a
	 * change made underneath it does not stick -- it silently reverts, and the artist is told nothing.
	 * Only the windows know what they hold, so this is asked here rather than behind the seam.
	 *
	 * @param repositoryRoot what `changing` is relative to; `heldOpen` is absolute.
	 * @return the held-open subset, still repository-relative, in the order `changing` gave them.
	 */
	[[nodiscard]] std::vector<QString>
	HeldOpenAmong(
		const std::filesystem::path& repositoryRoot,
		const std::vector<QString>&  changing,
		const QStringList&           heldOpen);

	/** What one pending change is called in the Version Control window. */
	[[nodiscard]] QString
	DescribeChange(ChangeKind kind);

	/**
	 * What to say to the user about `outcome`, in the words the menu uses.
	 *
	 * ADR-2 lives here: every sentence the Version Control window shows is written in this file, so
	 * nothing a backend produces reaches the user and replacing the backend does not change a word of
	 * it. Empty for kDone, which needs no explanation.
	 */
	[[nodiscard]] QString
	DescribeOutcome(const VersionControlOutcome& outcome);

	/**
	 * What to ask before throwing unsubmitted work away.
	 *
	 * The one action here with nowhere to get anything back from: Submit can be undone from the
	 * history, and this cannot be undone at all.
	 */
	[[nodiscard]] QString
	DescribeRevert(size_t count);

	/** Why the Version Control entries are greyed out on a project that has no version control. */
	[[nodiscard]] QString
	DescribeUnavailable();

	/** What to say when a window is holding assets open that a change would otherwise write over. */
	[[nodiscard]] QString
	DescribeHeldOpen(const std::vector<QString>& held);

	/** The message a submission starts with when the user types nothing of their own. */
	[[nodiscard]] QString
	DefaultSubmitMessage(const std::vector<PendingChange>& chosen);

	/** One line naming a submission in the history: when, who, and what they said. */
	[[nodiscard]] QString
	DescribeSubmission(const Submission& submission);
}
