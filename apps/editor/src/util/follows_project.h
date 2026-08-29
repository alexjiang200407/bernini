#pragma once

#include <QString>

class QObject;

namespace editor
{
	/**
	 * A panel that resolves asset paths against the open project's Data root, and so has to be told
	 * when a different project opens.
	 *
	 * A panel that only forwards the root to a child it owns implements this; the child does not,
	 * or it would be told twice and the owner would lose its say in the order.
	 */
	class IFollowsProject
	{
	public:
		virtual ~IFollowsProject() = default;

		/** Resolve against `dataRoot` from now on. Absolute, and the project's Data directory. */
		virtual void
		SetDataRoot(const QString& dataRoot) = 0;
	};

	/**
	 * Tells every follower in `root`'s object tree -- `root` itself included -- that `dataRoot` is
	 * the project's Data directory now.
	 *
	 * A walk rather than a list, for the reason GetAssetsHeldOpen is one: a panel added later is
	 * covered without anyone remembering it, where the hand-written calls this replaces were two.
	 */
	void
	SetProjectDataRoot(QObject* root, const QString& dataRoot);
}
