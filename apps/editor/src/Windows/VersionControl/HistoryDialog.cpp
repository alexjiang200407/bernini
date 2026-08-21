#include "Windows/VersionControl/HistoryDialog.h"

#include "Windows/VersionControl/version_control_rules.h"

#include <QListWidget>
#include <QPushButton>

HistoryDialog::HistoryDialog(const std::vector<editor::Submission>& history, QWidget* parent) :
	QDialog(parent), m_History(history), m_Widgets(editor::BuildHistoryUi(this))
{
	setWindowTitle("History");
	resize(560, 420);

	for (const editor::Submission& submission : m_History)
	{
		auto* row =
			new QListWidgetItem(editor::DescribeSubmission(submission), m_Widgets.submissions);
		row->setToolTip(
			static_cast<QStringList>(QList(submission.assets.begin(), submission.assets.end()))
				.join('\n'));
	}

	// Undoing nothing is not a thing to offer, and the list starts with nothing selected.
	m_Widgets.undo->setEnabled(false);
	connect(
		m_Widgets.submissions,
		&QListWidget::currentRowChanged,
		m_Widgets.undo,
		[this](int row) { m_Widgets.undo->setEnabled(row >= 0); });

	connect(m_Widgets.close, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_Widgets.undo, &QPushButton::clicked, this, [this] {
		m_Chosen = m_Widgets.submissions->currentRow();
		accept();
	});
}

std::optional<editor::Submission>
HistoryDialog::GetChosenSubmission() const
{
	if (m_Chosen < 0 || static_cast<size_t>(m_Chosen) >= m_History.size())
	{
		return std::nullopt;
	}
	return m_History[static_cast<size_t>(m_Chosen)];
}
