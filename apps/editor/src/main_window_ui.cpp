#include "main_window_ui.h"

#include <QAction>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

#include "util/editor_language.h"

namespace editor
{
	MainWindowWidgets
	BuildMainWindowUi(QMainWindow* parent)
	{
		auto widgets = MainWindowWidgets();

		parent->resize(1280, 720);

		QMenu* file =
			parent->menuBar()->addMenu(Localize("editor.main_window_ui.file_menu", "File"));
		widgets.fileMenu = file;

		widgets.newProject =
			file->addAction(Localize("editor.main_window_ui.new_project", "New Project..."));
		widgets.newProject->setShortcut(QKeySequence("Ctrl+N"));

		widgets.openProject =
			file->addAction(Localize("editor.main_window_ui.open_project", "Open Project..."));
		widgets.openProject->setShortcut(QKeySequence("Ctrl+O"));

		file->addSeparator();
		widgets.save = file->addAction(Localize("editor.main_window_ui.save", "Save"));

		file->addSeparator();
		widgets.cleanUnusedTextures = file->addAction(
			Localize("editor.main_window_ui.clean_unused_textures", "Clean Unused Textures..."));
		widgets.cleanUnusedTextures->setToolTip(Localize(
			"editor.main_window_ui.clean_unused_textures_tooltip",
			"Delete the baked textures that no material in this project references any more"));

		file->addSeparator();
		widgets.exit = file->addAction(Localize("editor.main_window_ui.exit", "Exit"));

		widgets.editMenu =
			parent->menuBar()->addMenu(Localize("editor.main_window_ui.edit_menu", "Edit"));
		widgets.windowMenu =
			parent->menuBar()->addMenu(Localize("editor.main_window_ui.window_menu", "Window"));

		return widgets;
	}
}
