#include "Windows/VersionControl/version_control_ui.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace editor
{
	PendingChangesWidgets
	BuildPendingChangesUi(QWidget* parent)
	{
		PendingChangesWidgets widgets;

		auto* rows = new QVBoxLayout(parent);
		rows->addWidget(new QLabel("Tick what to submit.", parent));

		widgets.changes = new QListWidget(parent);
		widgets.changes->setSelectionMode(QAbstractItemView::ExtendedSelection);
		rows->addWidget(widgets.changes, 1);

		rows->addWidget(new QLabel("Describe what you did:", parent));
		widgets.message = new QLineEdit(parent);
		rows->addWidget(widgets.message);

		auto* buttons  = new QHBoxLayout();
		widgets.revert = new QPushButton("Revert Ticked", parent);
		buttons->addWidget(widgets.revert);
		buttons->addStretch(1);
		widgets.close = new QPushButton("Close", parent);
		buttons->addWidget(widgets.close);
		widgets.submit = new QPushButton("Submit", parent);
		widgets.submit->setDefault(true);
		buttons->addWidget(widgets.submit);
		rows->addLayout(buttons);

		return widgets;
	}

	HistoryWidgets
	BuildHistoryUi(QWidget* parent)
	{
		HistoryWidgets widgets;

		auto* rows = new QVBoxLayout(parent);
		rows->addWidget(new QLabel("What has been submitted, newest first.", parent));

		widgets.submissions = new QListWidget(parent);
		rows->addWidget(widgets.submissions, 1);

		auto* buttons = new QHBoxLayout();
		buttons->addStretch(1);
		widgets.close = new QPushButton("Close", parent);
		buttons->addWidget(widgets.close);
		widgets.undo = new QPushButton("Undo This", parent);
		buttons->addWidget(widgets.undo);
		rows->addLayout(buttons);

		return widgets;
	}
}
