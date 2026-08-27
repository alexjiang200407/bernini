#pragma once

#include <QStringList>

class QObject;

namespace editor
{
	/**
	 * A panel with asset files open, and so a say in whether one may be deleted or renamed.
	 *
	 * Anything that only *displays* a file implements it too: a viewport lit by a `.benv` is the
	 * one thing that can hold one, because nothing on disk ever references a `.benv`.
	 */
	class IHoldsAssets
	{
	public:
		virtual ~IHoldsAssets() = default;

		/** The files this panel has open, in no order. Absolute, or relative to the working
		 *  directory -- GetAssetsHeldOpen's caller resolves them. */
		[[nodiscard]] virtual QStringList
		GetHeldOpenPaths() const = 0;
	};

	/**
	 * Every file the holders in `root`'s object tree -- `root` itself included -- have open.
	 *
	 * A walk rather than a list, because the list it replaces was written by hand and named two of
	 * the six panels that hold assets. Answered at each deletion, so there is no copy to go stale.
	 */
	[[nodiscard]] QStringList
	GetAssetsHeldOpen(const QObject* root);
}
