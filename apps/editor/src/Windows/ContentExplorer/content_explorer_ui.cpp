#include "content_explorer_ui.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QListView>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <qnamespace.h>
#include <qsizepolicy.h>
#include <qstringliteral.h>

namespace editor
{
	ContentExplorerWidgets
	BuildContentExplorerUi(QWidget* parent)
	{
		auto widgets = ContentExplorerWidgets();

		// The size the pane opens at when it is a window of its own rather than docked, which is
		// what a test builds.
		parent->resize(706, 276);

		auto* rootLayout = new QHBoxLayout(parent);

		// A folder tree down the left of the folder's contents, so an asset can be found and
		// dragged straight out of the tree without navigating to its folder on the right.
		widgets.splitter = new QSplitter(Qt::Horizontal, parent);
		rootLayout->addWidget(widgets.splitter);

		widgets.fileExplorer = new QTreeView(widgets.splitter);
		widgets.fileExplorer->setObjectName("FileExplorer");
		widgets.fileExplorer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		auto* currentDirectoryPane = new QWidget(widgets.splitter);
		currentDirectoryPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

		auto* paneLayout = new QVBoxLayout(currentDirectoryPane);
		paneLayout->setContentsMargins(0, 0, 0, 0);

		widgets.backButton = new QToolButton(currentDirectoryPane);
		widgets.backButton->setObjectName("BackButton");
		widgets.backButton->setText(QStringLiteral("Back"));
		widgets.backButton->setToolTip(QStringLiteral("Back to the folder shown before"));
		widgets.backButton->setEnabled(false);

		widgets.modeSelector = new QComboBox(currentDirectoryPane);
		widgets.modeSelector->setObjectName("ModeSelector");
		widgets.modeSelector->setToolTip(QStringLiteral(
			"What the browser shows: the project's authored assets, or the textures "
			"its imports extracted"));

		// The order the window reads back as a BrowseMode: index 1 is textures, anything else is
		// assets.
		widgets.modeSelector->addItem(QStringLiteral("Assets"));
		widgets.modeSelector->addItem(QStringLiteral("Textures"));

		auto* navigation = new QHBoxLayout();
		navigation->addWidget(widgets.backButton);
		navigation->addWidget(widgets.modeSelector);
		navigation->addStretch();
		paneLayout->addLayout(navigation);

		widgets.currentDirectory = new QListView(currentDirectoryPane);
		widgets.currentDirectory->setObjectName("CurrentDirectoryExplorer");
		widgets.currentDirectory->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		paneLayout->addWidget(widgets.currentDirectory);

		return widgets;
	}
}
