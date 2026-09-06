#include "main_window_ui.h"

#include "util/QtSupport.h"  // IWYU pragma: keep

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <catch2/catch_test_macros.hpp>
#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qstringliteral.h>

// The menu bar was a Designer form until it was built here, and a form is the kind of thing whose
// contents nobody notices going missing. These cases are what noticed it before: the builder needs
// no graphics device, so unlike the rest of [mainwindow] they cost nothing to run.

namespace
{
	/** The menu titled `title` on `window`'s bar, or null when it has none. */
	QMenu*
	MenuNamed(const QMainWindow& window, const QString& title)
	{
		for (const QAction* action : window.menuBar()->actions())
		{
			if (action->menu() != nullptr && action->menu()->title() == title)
				return action->menu();
		}
		return nullptr;
	}

	/** The titles of `window`'s menus, left to right. */
	QStringList
	MenuTitles(const QMainWindow& window)
	{
		auto titles = QStringList();
		for (const QAction* action : window.menuBar()->actions())
		{
			if (action->menu() != nullptr)
				titles << action->menu()->title();
		}
		return titles;
	}
}

TEST_CASE("The menu bar opens with File, Edit and Window in that order", "[mainwindow][menu]")
{
	auto window = QMainWindow();

	[[maybe_unused]] const editor::MainWindowWidgets widgets = editor::BuildMainWindowUi(&window);

	// Render is deliberately absent: what it offers depends on the viewports, which do not exist
	// when this runs, so MainWindow adds it afterwards and it lands to the right of these three.
	CHECK(MenuTitles(window) == QStringList({ "File", "Edit", "Window" }));
}

TEST_CASE("The File menu keeps its actions, their order and their groups", "[mainwindow][menu]")
{
	auto window = QMainWindow();

	const editor::MainWindowWidgets widgets = editor::BuildMainWindowUi(&window);

	const QMenu* file = MenuNamed(window, "File");
	REQUIRE(file != nullptr);

	// The separators are pinned with the entries: they are what keeps a destructive item away from
	// the one above it, and Clean Unused Textures deletes files.
	const QList<QAction*> actions = file->actions();
	REQUIRE(actions.size() == 8);

	CHECK(actions[0] == widgets.newProject);
	CHECK(actions[1] == widgets.openProject);
	CHECK(actions[2]->isSeparator());
	CHECK(actions[3] == widgets.save);
	CHECK(actions[4]->isSeparator());
	CHECK(actions[5] == widgets.cleanUnusedTextures);
	CHECK(actions[6]->isSeparator());
	CHECK(actions[7] == widgets.exit);

	CHECK(widgets.newProject->text() == QStringLiteral("New Project..."));
	CHECK(widgets.openProject->text() == QStringLiteral("Open Project..."));
	CHECK(widgets.save->text() == QStringLiteral("Save"));
	CHECK(widgets.cleanUnusedTextures->text() == QStringLiteral("Clean Unused Textures..."));
	CHECK(widgets.exit->text() == QStringLiteral("Exit"));

	// The one entry whose name does not say what it destroys.
	CHECK_FALSE(widgets.cleanUnusedTextures->toolTip().isEmpty());
}

TEST_CASE("New and Open keep their shortcuts", "[mainwindow][menu]")
{
	auto window = QMainWindow();

	const editor::MainWindowWidgets widgets = editor::BuildMainWindowUi(&window);

	// A shortcut is the half of a menu entry nobody looks at, so it is the half that goes missing.
	CHECK(widgets.newProject->shortcut() == QKeySequence(QStringLiteral("Ctrl+N")));
	CHECK(widgets.openProject->shortcut() == QKeySequence(QStringLiteral("Ctrl+O")));

	// Save has none, and never had: it would fire while a graph edit is in a spin box.
	CHECK(widgets.save->shortcut().isEmpty());
}

TEST_CASE("Edit and Window start empty", "[mainwindow][menu]")
{
	auto window = QMainWindow();

	const editor::MainWindowWidgets widgets = editor::BuildMainWindowUi(&window);

	REQUIRE(widgets.editMenu != nullptr);
	REQUIRE(widgets.windowMenu != nullptr);

	// Window is filled with the docks' toggles, which do not exist yet. Anything already in it here
	// would sit above them.
	CHECK(widgets.editMenu->actions().isEmpty());
	CHECK(widgets.windowMenu->actions().isEmpty());
}
