#include "Windows/VersionControl/VersionControlActions.h"

#include "Async/BackgroundTask.h"
#include "Windows/VersionControl/HistoryDialog.h"
#include "Windows/VersionControl/PendingChangesDialog.h"
#include "Windows/VersionControl/version_control_rules.h"

#include <QMessageBox>
#include <QWidget>

VersionControlActions::VersionControlActions(
	QWidget*                                 parent,
	std::unique_ptr<editor::IVersionControl> versionControl,
	std::function<QStringList()>             heldOpen) :
	QObject(parent), m_VersionControl(std::move(versionControl)), m_HeldOpen(std::move(heldOpen))
{}

QWidget*
VersionControlActions::GetParentWidget() const
{
	return qobject_cast<QWidget*>(parent());
}

void
VersionControlActions::Run(
	const QString&                                        title,
	const std::function<editor::VersionControlOutcome()>& verb)
{
	editor::VersionControlOutcome outcome;

	const auto task = background::RunWithLoadingScreen(
		GetParentWidget(),
		title,
		[&outcome, &verb](background::Progress&) { outcome = verb(); });

	if (task.Failed())
	{
		QMessageBox::warning(GetParentWidget(), title, task.error);
		return;
	}
	if (const QString said = editor::DescribeOutcome(outcome); !said.isEmpty())
	{
		QMessageBox::information(GetParentWidget(), title, said);
	}
}

bool
VersionControlActions::IsHeldOpen(const QString& title, const std::vector<QString>& changing)
{
	const auto held = editor::HeldOpenAmong(m_VersionControl->GetRoot(), changing, m_HeldOpen());
	if (held.empty())
	{
		return false;
	}
	QMessageBox::information(GetParentWidget(), title, editor::DescribeHeldOpen(held));
	return true;
}

void
VersionControlActions::GetLatest()
{
	if (!m_VersionControl)
	{
		return;
	}

	const QString title = "Get Latest";

	// What it would bring in, before it brings any of it in: the windows have to be asked about the
	// assets themselves, and only the backend knows which those are.
	std::vector<QString> incoming;
	const auto           looked = background::RunWithLoadingScreen(
		GetParentWidget(),
		title,
		[this, &incoming](background::Progress&) { incoming = m_VersionControl->ListIncoming(); });

	if (looked.Failed())
	{
		QMessageBox::warning(GetParentWidget(), title, looked.error);
		return;
	}
	if (IsHeldOpen(title, incoming))
	{
		return;
	}

	Run(title, [this] { return m_VersionControl->GetLatest(); });
}

void
VersionControlActions::ShowPendingChanges()
{
	if (!m_VersionControl)
	{
		return;
	}

	const QString title = "Submit Changes";

	std::vector<editor::PendingChange> changes;
	const auto                         listed = background::RunWithLoadingScreen(
		GetParentWidget(),
		title,
		[this, &changes](background::Progress&) { changes = m_VersionControl->ListChanges(); });

	if (listed.Failed())
	{
		QMessageBox::warning(GetParentWidget(), title, listed.error);
		return;
	}
	if (changes.empty())
	{
		QMessageBox::information(
			GetParentWidget(),
			title,
			editor::DescribeOutcome({ .status = editor::VersionControlStatus::kNothingToDo }));
		return;
	}

	PendingChangesDialog dialog(changes, GetParentWidget());
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	const auto chosen = dialog.GetChosenAssets();
	if (dialog.GetAction() == PendingChangesDialog::Action::kSubmit)
	{
		const QString message = dialog.GetMessage();
		Run(title, [this, chosen, message] { return m_VersionControl->Submit(chosen, message); });
		return;
	}

	const auto confirmed = QMessageBox::warning(
		GetParentWidget(),
		"Revert",
		editor::DescribeRevert(chosen.size()),
		QMessageBox::Cancel | QMessageBox::Discard,
		QMessageBox::Cancel);

	if (confirmed != QMessageBox::Discard || IsHeldOpen("Revert", chosen))
	{
		return;
	}
	Run("Revert", [this, chosen] { return m_VersionControl->Revert(chosen); });
}

void
VersionControlActions::ShowHistory()
{
	if (!m_VersionControl)
	{
		return;
	}

	const QString title = "History";

	std::vector<editor::Submission> history;
	const auto                      listed = background::RunWithLoadingScreen(
		GetParentWidget(),
		title,
		[this, &history](background::Progress&) {
			history = m_VersionControl->ListHistory(c_HistoryDepth);
		});

	if (listed.Failed())
	{
		QMessageBox::warning(GetParentWidget(), title, listed.error);
		return;
	}

	HistoryDialog dialog(history, GetParentWidget());
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	const auto chosen = dialog.GetChosenSubmission();
	if (!chosen.has_value() || IsHeldOpen(title, chosen->assets))
	{
		return;
	}

	const QString id = chosen->id;
	Run(title, [this, id] { return m_VersionControl->UndoSubmission(id); });
}

void
VersionControlActions::RevertEverything()
{
	if (!m_VersionControl)
	{
		return;
	}

	const QString title = "Revert All Unsubmitted Changes";

	std::vector<editor::PendingChange> changes;
	const auto                         listed = background::RunWithLoadingScreen(
		GetParentWidget(),
		title,
		[this, &changes](background::Progress&) { changes = m_VersionControl->ListChanges(); });

	if (listed.Failed())
	{
		QMessageBox::warning(GetParentWidget(), title, listed.error);
		return;
	}
	if (changes.empty())
	{
		QMessageBox::information(
			GetParentWidget(),
			title,
			editor::DescribeOutcome({ .status = editor::VersionControlStatus::kNothingToDo }));
		return;
	}

	std::vector<QString> assets;
	assets.reserve(changes.size());
	for (const editor::PendingChange& change : changes)
	{
		assets.push_back(change.path);
	}

	const auto confirmed = QMessageBox::warning(
		GetParentWidget(),
		title,
		editor::DescribeRevert(assets.size()),
		QMessageBox::Cancel | QMessageBox::Discard,
		QMessageBox::Cancel);

	if (confirmed != QMessageBox::Discard || IsHeldOpen(title, assets))
	{
		return;
	}
	Run(title, [this, assets] { return m_VersionControl->Revert(assets); });
}
