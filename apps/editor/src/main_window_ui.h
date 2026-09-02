#pragma once

class QAction;
class QMainWindow;
class QMenu;

namespace editor
{
	/** The menu bar BuildMainWindowUi creates, so the window can connect and drive it. */
	struct MainWindowWidgets
	{
		QMenu* editMenu = nullptr;

		// Left empty: the dock toggles that fill it do not exist until the docks do.
		QMenu* windowMenu = nullptr;

		QAction* newProject          = nullptr;
		QAction* openProject         = nullptr;
		QAction* save                = nullptr;
		QAction* cleanUnusedTextures = nullptr;
		QAction* exit                = nullptr;
	};

	/**
	 * Builds `parent`'s File, Edit and Window menus, and sizes the window to the extent it opens at.
	 * The Render menu is not here: what it offers depends on the viewports, which do not exist yet.
	 *
	 * Nothing is connected -- what each action *does* is the window's, and keeping the two apart is
	 * what lets this be read as a layout rather than as behaviour.
	 */
	[[nodiscard]] MainWindowWidgets
	BuildMainWindowUi(QMainWindow* parent);
}
