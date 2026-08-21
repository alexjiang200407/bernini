#include "Windows/VersionControl/PendingChangesDialog.h"

#include "Windows/VersionControl/version_control_rules.h"

#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

namespace
{
	constexpr auto c_ChangeIndex = Qt::UserRole;
}

PendingChangesDialog::PendingChangesDialog(
	const std::vector<editor::PendingChange>& changes,
	QWidget*                                  parent) :
	QDialog(parent), m_Changes(changes), m_Widgets(editor::BuildPendingChangesUi(this))
{
	setWindowTitle("Submit Changes");
	resize(520, 420);

	for (size_t at = 0; at < m_Changes.size(); ++at)
	{
		const editor::PendingChange& change = m_Changes[at];

		auto* row = new QListWidgetItem(
			QStringLiteral("%1  —  %2")
				.arg(editor::DescribeChange(change.kind), QFileInfo(change.path).fileName()),
			m_Widgets.changes);

		row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
		row->setCheckState(Qt::Checked);
		row->setToolTip(change.path);
		row->setData(c_ChangeIndex, static_cast<qulonglong>(at));
	}

	RefreshMessagePlaceholder();

	connect(
		m_Widgets.changes,
		&QListWidget::itemChanged,
		this,
		&PendingChangesDialog::RefreshMessagePlaceholder);

	connect(m_Widgets.close, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_Widgets.submit, &QPushButton::clicked, this, [this] {
		m_Action = Action::kSubmit;
		accept();
	});
	connect(m_Widgets.revert, &QPushButton::clicked, this, [this] {
		m_Action = Action::kRevert;
		accept();
	});
}

std::vector<editor::PendingChange>
PendingChangesDialog::GetChosenChanges() const
{
	std::vector<editor::PendingChange> chosen;
	for (int row = 0; row < m_Widgets.changes->count(); ++row)
	{
		const QListWidgetItem* item = m_Widgets.changes->item(row);
		if (item->checkState() == Qt::Checked)
		{
			chosen.push_back(m_Changes[item->data(c_ChangeIndex).toULongLong()]);
		}
	}
	return chosen;
}

std::vector<QString>
PendingChangesDialog::GetChosenAssets() const
{
	std::vector<QString> assets;
	for (const editor::PendingChange& change : GetChosenChanges())
	{
		assets.push_back(change.path);
	}
	return assets;
}

QString
PendingChangesDialog::GetMessage() const
{
	const QString typed = m_Widgets.message->text().trimmed();
	return typed.isEmpty() ? m_Widgets.message->placeholderText() : typed;
}

void
PendingChangesDialog::RefreshMessagePlaceholder()
{
	// A placeholder rather than filled-in text, so a message the user has typed is never replaced by
	// ticking another asset -- and leaving it alone still submits something legible.
	m_Widgets.message->setPlaceholderText(editor::DefaultSubmitMessage(GetChosenChanges()));
}
