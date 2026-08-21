#pragma once

#include "VersionControl/IVersionControl.h"

#include <QObject>

/**
 * What the Version Control menu does, off the window that hosts it.
 *
 * Owns the seam, asks the windows what they are holding open before anything writes, and turns every
 * refusal into a sentence. Not a window: it opens dialogs and reports, and touches no view.
 *
 * Inert with no repository -- `IsAvailable` is false and every action is a no-op -- which is ADR-6.
 */
class VersionControlActions : public QObject
{
	Q_OBJECT

public:
	/**
	 * @param parent the widget dialogs and messages are shown over; also the QObject parent.
	 * @param versionControl the project's, or null when it is not in a repository.
	 * @param heldOpen asked afresh before each change, so there is no copy of the answer to go stale.
	 */
	VersionControlActions(
		QWidget*                                 parent,
		std::unique_ptr<editor::IVersionControl> versionControl,
		std::function<QStringList()>             heldOpen);

	[[nodiscard]] bool
	IsAvailable() const noexcept
	{
		return m_VersionControl != nullptr;
	}

	void
	GetLatest();

	void
	ShowPendingChanges();

	void
	ShowHistory();

	/**
	 * Throws away everything unsubmitted, after asking.
	 *
	 * The same verb the dialog offers on a ticked few, reached in one step for the case it is
	 * actually wanted in: the project is a mess and the last submission was fine.
	 */
	void
	RevertEverything();

private:
	/** How many submissions the History dialog offers. Older than this is a job for a programmer. */
	static constexpr auto c_HistoryDepth = 50;

	/** Runs `verb` behind the loading screen and says whatever it refused. */
	void
	Run(const QString& title, const std::function<editor::VersionControlOutcome()>& verb);

	/**
	 * Whether a window is holding any of `changing` open, saying so if it is.
	 *
	 * ADR-10's other half, and the reason it is here: only the windows know what they hold.
	 */
	[[nodiscard]] bool
	IsHeldOpen(const QString& title, const std::vector<QString>& changing);

	[[nodiscard]] QWidget*
	GetParentWidget() const;

	std::unique_ptr<editor::IVersionControl> m_VersionControl;
	std::function<QStringList()>             m_HeldOpen;
};
