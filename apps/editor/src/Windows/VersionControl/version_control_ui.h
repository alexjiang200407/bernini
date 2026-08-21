#pragma once

class QLineEdit;
class QListWidget;
class QPushButton;
class QWidget;

namespace editor
{
	/** What the Pending Changes dialog is made of. Built, never connected. */
	struct PendingChangesWidgets
	{
		QListWidget* changes = nullptr;
		QLineEdit*   message = nullptr;
		QPushButton* submit  = nullptr;
		QPushButton* revert  = nullptr;
		QPushButton* close   = nullptr;
	};

	[[nodiscard]] PendingChangesWidgets
	BuildPendingChangesUi(QWidget* parent);

	/** What the History dialog is made of. Built, never connected. */
	struct HistoryWidgets
	{
		QListWidget* submissions = nullptr;
		QPushButton* undo        = nullptr;
		QPushButton* close       = nullptr;
	};

	[[nodiscard]] HistoryWidgets
	BuildHistoryUi(QWidget* parent);
}
