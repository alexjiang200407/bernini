#pragma once

#include "VersionControl/IVersionControl.h"
#include "Windows/VersionControl/version_control_ui.h"

#include <QDialog>

/**
 * What has been submitted, and which entry to undo.
 *
 * A dialog only, as PendingChangesDialog is: the caller reads the history and acts on the choice.
 */
class HistoryDialog : public QDialog
{
	Q_OBJECT

public:
	explicit HistoryDialog(
		const std::vector<editor::Submission>& history,
		QWidget*                               parent = nullptr);

	/** The entry to undo, or nothing when the dialog was closed without choosing one. */
	[[nodiscard]] std::optional<editor::Submission>
	GetChosenSubmission() const;

private:
	std::vector<editor::Submission> m_History;
	editor::HistoryWidgets          m_Widgets;
	int                             m_Chosen = -1;
};
