#include "Windows/VersionControl/HistoryDialog.h"
#include "Windows/VersionControl/PendingChangesDialog.h"

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

namespace
{
	editor::PendingChange
	Change(const char* path, editor::ChangeKind kind)
	{
		return { .path = QString::fromUtf8(path), .renamedFrom = std::nullopt, .kind = kind };
	}

	const std::vector<editor::PendingChange> c_Changes{
		Change("Data/Meshes/coyote.bmesh", editor::ChangeKind::kModified),
		Change("Data/Meshes/squirrel.bmesh", editor::ChangeKind::kAdded),
	};

	/** The dialog's list, which the dialog does not hand out -- found the way a user would see it. */
	QListWidget*
	ListOf(const QDialog& dialog)
	{
		const auto lists = dialog.findChildren<QListWidget*>();
		REQUIRE(lists.size() == 1);
		return lists.front();
	}
}

TEST_CASE("Everything the project has changed starts out chosen", "[vcs]")
{
	const PendingChangesDialog dialog(c_Changes);

	CHECK(ListOf(dialog)->count() == 2);
	CHECK(dialog.GetChosenAssets().size() == 2);

	// Nothing has been pressed, so nothing should look as though it has.
	CHECK(dialog.GetAction() == PendingChangesDialog::Action::kNone);
}

TEST_CASE("Only the ticked assets are submitted", "[vcs]")
{
	const PendingChangesDialog dialog(c_Changes);
	ListOf(dialog)->item(0)->setCheckState(Qt::Unchecked);

	CHECK(dialog.GetChosenAssets() == std::vector<QString>{ "Data/Meshes/squirrel.bmesh" });

	// The kind travels with the path, so a caller can describe what it is about to do.
	REQUIRE(dialog.GetChosenChanges().size() == 1);
	CHECK(dialog.GetChosenChanges().front().kind == editor::ChangeKind::kAdded);
}

// A message the user is part-way through typing must survive them ticking another asset.
TEST_CASE(
	"The suggested message follows what is ticked, but never overwrites what is typed",
	"[vcs]")
{
	const PendingChangesDialog dialog(c_Changes);
	const auto                 fields = dialog.findChildren<QLineEdit*>();
	REQUIRE(fields.size() == 1);

	CHECK(dialog.GetMessage() == QString("Updated 2 assets"));

	ListOf(dialog)->item(0)->setCheckState(Qt::Unchecked);
	CHECK(dialog.GetMessage() == QString("New squirrel.bmesh"));

	fields.front()->setText("bob adds a squirrel");
	ListOf(dialog)->item(0)->setCheckState(Qt::Checked);
	CHECK(dialog.GetMessage() == QString("bob adds a squirrel"));
}

TEST_CASE("Submit and Revert are told apart by which one was pressed", "[vcs]")
{
	SECTION("Submit")
	{
		PendingChangesDialog dialog(c_Changes);

		for (QPushButton* button : dialog.findChildren<QPushButton*>())
		{
			if (button->text() == "Submit")
				button->click();
		}
		CHECK(dialog.GetAction() == PendingChangesDialog::Action::kSubmit);
	}

	SECTION("Revert")
	{
		PendingChangesDialog dialog(c_Changes);
		for (QPushButton* button : dialog.findChildren<QPushButton*>())
		{
			if (button->text() == "Revert Ticked")
				button->click();
		}
		CHECK(dialog.GetAction() == PendingChangesDialog::Action::kRevert);
	}
}

TEST_CASE("The history offers nothing to undo until an entry is picked", "[vcs]")
{
	editor::Submission first;
	first.id      = "aaaa";
	first.author  = "Bernini Test";
	first.message = "bob adds a squirrel";
	first.when    = QDateTime::fromString("2026-08-22T09:30:00+00:00", Qt::ISODate);
	first.assets  = { "Data/Meshes/squirrel.bmesh" };

	editor::Submission second = first;
	second.id                 = "bbbb";
	second.message            = "bob adds some apples";

	HistoryDialog dialog({ first, second });

	CHECK(ListOf(dialog)->count() == 2);
	CHECK_FALSE(dialog.GetChosenSubmission().has_value());

	for (QPushButton* button : dialog.findChildren<QPushButton*>())
	{
		if (button->text() == "Undo This")
		{
			CHECK_FALSE(button->isEnabled());

			ListOf(dialog)->setCurrentRow(1);
			CHECK(button->isEnabled());
			button->click();
		}
	}

	REQUIRE(dialog.GetChosenSubmission().has_value());
	CHECK(dialog.GetChosenSubmission()->id == QString("bbbb"));
}
