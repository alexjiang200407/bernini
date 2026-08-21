#pragma once

#include <QDateTime>
#include <QString>

namespace editor
{
	/** What happened to one asset, in the four kinds the Version Control UI names. */
	enum class ChangeKind
	{
		kAdded,
		kModified,
		kDeleted,
		kRenamed,
	};

	/** One asset the project has changed since the last submission. */
	struct PendingChange
	{
		// Relative to the repository root and `/`-separated, so it reads the same on every platform
		// and can be handed straight back to the backend.
		QString path;

		// Set exactly when kind is kRenamed.
		std::optional<QString> renamedFrom;

		ChangeKind kind = ChangeKind::kModified;
	};

	/** What a verb did, or the reason it declined to. */
	enum class VersionControlStatus
	{
		kDone,
		kNoIdentity,           // who is submitting has never been set up
		kNothingToDo,          // nothing was chosen, or there is nothing to get
		kCouldNotReachShared,  // the shared project could not be reached at all
		kWorkHasMovedOn,       // Submit and Undo: the shared project has work this one does not
		kWouldNotFastForward,  // Get Latest: this project has submitted work the shared one does not
		kAssetsInTheWay,      // Get Latest and Undo: unsubmitted changes the change would overwrite
		kAssetsStillInUse,    // any verb that deletes: an asset something else still needs would go
		kAssetsChangedSince,  // Undo: later submissions have changed the assets it would restore
	};

	/** What a verb answered with. */
	struct VersionControlOutcome
	{
		VersionControlStatus status = VersionControlStatus::kDone;

		// What the status is *about*, so the UI can name assets rather than describe a backend.
		// Empty whenever the status is about nothing in particular, kDone included.
		std::vector<QString> assets;

		// Parallel to `assets`, and only for kAssetsStillInUse: something staying behind that still
		// uses assets[i]. Naming one without the other says a referrer would be removed, which it
		// would not.
		std::vector<QString> neededBy;
	};

	/** One entry in the project's history: a submission somebody made. */
	struct Submission
	{
		// The backend's own name for this entry. Opaque: the UI carries it back to UndoSubmission
		// and neither shows nor interprets it.
		QString id;

		QString   author;
		QDateTime when;
		QString   message;

		// Repository-relative, as ListChanges reports them.
		std::vector<QString> assets;
	};

	/**
	 * What the Version Control UI talks to, so that it never talks to a backend.
	 *
	 * No type from any particular backend crosses this seam and no message any implementation
	 * produces is shown to the user: the UI writes its own words, in the vocabulary the menu uses.
	 *
	 * Two different failures, kept apart deliberately. A *refusal* is an ordinary answer the user
	 * can act on -- work has moved on, this would conflict -- and is carried in the return value of
	 * the verb that refuses. An *exception* means the repository could not be read at all, which is
	 * not something the user chose or can resolve here.
	 */
	class IVersionControl
	{
	public:
		virtual ~IVersionControl() = default;

		/**
		 * What every path this reports, and every path it accepts, is relative to.
		 *
		 * Not necessarily the project directory: a project can sit inside a larger repository, and the
		 * paths are the repository's.
		 */
		[[nodiscard]] virtual const std::filesystem::path&
		GetRoot() const noexcept = 0;

		/**
		 * Every asset changed since the last submission, ordered by path.
		 *
		 * Assets the project ignores are never listed: a build product nobody submits is not a
		 * pending change.
		 *
		 * @throws std::runtime_error if the repository cannot be read.
		 */
		[[nodiscard]] virtual std::vector<PendingChange>
		ListChanges() const = 0;

		/**
		 * Records `assets` against `message` and publishes them, as one action.
		 *
		 * Refuses before changing anything when the shared project has moved on, so a recorded change
		 * that cannot be published never exists -- work that looks saved and is not.
		 *
		 * @param assets repository-relative paths, as ListChanges reports them.
		 * @throws std::runtime_error if the repository cannot be read or written.
		 */
		[[nodiscard]] virtual VersionControlOutcome
		Submit(const std::vector<QString>& assets, const QString& message) = 0;

		/**
		 * Brings the shared project's work into this one.
		 *
		 * Fast-forwards or refuses, never merges: a fast-forward is the one update that cannot leave
		 * assets in a state the user has no way out of. A refusal leaves the assets exactly as they
		 * were.
		 *
		 * @throws std::runtime_error if the repository cannot be read or written.
		 */
		[[nodiscard]] virtual VersionControlOutcome
		GetLatest() = 0;

		/**
		 * The assets Get Latest would change, without changing any of them.
		 *
		 * Asks the shared project first, so what it reports is current rather than whatever was last
		 * heard -- which is why it is not const-cheap and is asked once per menu action rather than
		 * per repaint.
		 *
		 * @throws std::runtime_error if the repository cannot be read.
		 */
		[[nodiscard]] virtual std::vector<QString>
		ListIncoming() = 0;

		/**
		 * Puts `assets` back as they were at the last submission, losing what was done to them since.
		 *
		 * An asset that was never submitted has nothing to go back to, so reverting one removes it.
		 *
		 * @param assets repository-relative paths, as ListChanges reports them.
		 * @throws std::runtime_error if the repository cannot be read or written.
		 */
		[[nodiscard]] virtual VersionControlOutcome
		Revert(const std::vector<QString>& assets) = 0;

		/**
		 * The last `limit` submissions, newest first.
		 *
		 * @throws std::runtime_error if `limit` is not positive, or the history cannot be read.
		 */
		[[nodiscard]] virtual std::vector<Submission>
		ListHistory(int limit) const = 0;

		/**
		 * Puts the assets a submission touched back as they were before it, and publishes *that* as
		 * the newest submission.
		 *
		 * The undone entry stays in the history: the record of what happened is most of what a
		 * history is for, and rewriting it would leave this project unable to publish again.
		 *
		 * @param id as ListHistory reported it.
		 * @throws std::runtime_error if the repository cannot be read or written.
		 */
		[[nodiscard]] virtual VersionControlOutcome
		UndoSubmission(const QString& id) = 0;
	};
}
