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
	class IRevisionControl
	{
	public:
		virtual ~IRevisionControl() = default;

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
	};
}
