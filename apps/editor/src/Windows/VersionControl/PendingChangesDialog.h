#pragma once

#include "VersionControl/IVersionControl.h"
#include "Windows/VersionControl/version_control_ui.h"

#include <QDialog>

/**
 * What the project has changed since the last submission, and what to do about it.
 *
 * A dialog only: it reads nothing and starts nothing. The caller lists the changes, and acts on
 * whatever comes back -- so what the dialog decides can be driven by a test with no repository
 * anywhere near it.
 */
class PendingChangesDialog : public QDialog
{
	Q_OBJECT

public:
	/** Which button closed the dialog. */
	enum class Action
	{
		kNone,
		kSubmit,
		kRevert,
	};

	explicit PendingChangesDialog(
		const std::vector<editor::PendingChange>& changes,
		QWidget*                                  parent = nullptr);

	[[nodiscard]] Action
	GetAction() const noexcept
	{
		return m_Action;
	}

	/** The assets ticked when the dialog closed, repository-relative. */
	[[nodiscard]] std::vector<QString>
	GetChosenAssets() const;

	/** The ticked changes, for a caller that needs their kinds as well as their paths. */
	[[nodiscard]] std::vector<editor::PendingChange>
	GetChosenChanges() const;

	/** What the user typed, or the message the dialog started with if they left it alone. */
	[[nodiscard]] QString
	GetMessage() const;

private:
	void
	RefreshMessagePlaceholder();

	std::vector<editor::PendingChange> m_Changes;
	editor::PendingChangesWidgets      m_Widgets;
	Action                             m_Action = Action::kNone;
};
