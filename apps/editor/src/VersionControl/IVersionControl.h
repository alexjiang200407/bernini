#pragma once

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
		kWorkHasMovedOn,       // Submit: the shared project has work this one does not
		kWouldNotFastForward,  // Get Latest: this project has submitted work the shared one does not
		kAssetsInTheWay,       // Get Latest: unsubmitted changes the update would overwrite
		kAssetsStillInUse,  // any verb that deletes: an asset something else still needs would go
	};

	/** What a verb answered with. */
	struct VersionControlOutcome
	{
		VersionControlStatus status = VersionControlStatus::kDone;

		// What the status is *about*, so the UI can name assets rather than describe a backend.
		// Empty whenever the status is about nothing in particular, kDone included.
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
		 * Puts `assets` back as they were at the last submission, losing what was done to them since.
		 *
		 * An asset that was never submitted has nothing to go back to, so reverting one removes it.
		 *
		 * @param assets repository-relative paths, as ListChanges reports them.
		 * @throws std::runtime_error if the repository cannot be read or written.
		 */
		[[nodiscard]] virtual VersionControlOutcome
		Revert(const std::vector<QString>& assets) = 0;
	};
}
