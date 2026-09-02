#pragma once

class QComboBox;
class QListView;
class QSplitter;
class QToolButton;
class QTreeView;
class QWidget;

namespace editor
{
	/**
	 * The widgets BuildContentExplorerUi creates, so the window can connect and drive them.
	 *
	 * Each carries the object name a test reaches it by -- `FileExplorer`,
	 * `CurrentDirectoryExplorer`, `BackButton`, `ModeSelector`. They are interface, not decoration.
	 */
	struct ContentExplorerWidgets
	{
		QSplitter*   splitter         = nullptr;
		QTreeView*   fileExplorer     = nullptr;
		QListView*   currentDirectory = nullptr;
		QToolButton* backButton       = nullptr;
		QComboBox*   modeSelector     = nullptr;
	};

	/**
	 * Builds the content explorer's two panes under `parent`, in the state they start in: Back
	 * disabled, the mode selector on Assets, neither view given a model.
	 *
	 * Nothing is connected -- what each widget *does* is the window's, and keeping the two apart is
	 * what lets this be read as a layout rather than as behaviour.
	 */
	[[nodiscard]] ContentExplorerWidgets
	BuildContentExplorerUi(QWidget* parent);
}
